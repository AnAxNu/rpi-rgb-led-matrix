// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2018 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

#include "pixel-mapper.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>

namespace rgb_matrix {
namespace {

// Put panels in a single row, that has been put in multiple rows by using parallel chains.
// 6 panels, in 3 parallel chains with 2 in each chain (--led-chain=2 --led-parallel=3)
// normally looks like this:
// [<][<]
// [<][<]
// [<][<]
//
// can be arranged in one long row:
// [<][<][<][<][<][<]
//
// This is useful if you are using a hat/adapter with multiple parallel chains
// but trying to run code made for a single chain.
//
// Parameters can be used to get the text-scroller to scroll around the cube sides.
// The parameter can be set to only use the horizontal panel-sides (parameter:H) of the cube
// (not top/bottom panel) or the only use vertical (parameter:V) panel-sides.
// Parameter string should be either empty (not used), H for horizontal, or V for vertical.
// Parameter string example: Row-mapper:H
class RowArrangementMapper : public PixelMapper {
public:
  enum Mode { normal, band_horizontal, band_vertical };

  RowArrangementMapper() : parallel_(1) {}

  virtual const char *GetName() const { return "Row-mapper"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {
    if (parallel < 2) {  // technically, a chain of 1 would work, but somewhat pointless
      fprintf(stderr, "%s: need at least --led-parallel=2 for usefullness\n",this->GetName());
      return false;
    }

    Mode mode = normal;

    if(param == NULL || strlen(param) == 0) {
      mode = normal;
    }else if(strlen(param) != 1) {
      fprintf(stderr, "%s parameter should be a single character:'V' or 'H'\n",this->GetName());
    }else{
      switch (*param) {
        case 'V':
        case 'v':
        mode = band_vertical;
        break;
      case 'H':
      case 'h':
        mode = band_horizontal;
        break;
      default:
        fprintf(stderr, "%s parameter should be either 'V' or 'H'\n",this->GetName());
        return false;
      }
    }

    chain_ = chain;
    parallel_ = parallel;
    mode_ = mode;

    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height)
    const {

      const int panel_width = matrix_width / chain_;

      switch (mode_) {
        case normal:
          *visible_height = matrix_height / parallel_;
          *visible_width = matrix_width * parallel_;
          break;
        case band_vertical:
          *visible_height = matrix_height / parallel_;
          *visible_width = (matrix_width * parallel_) - (panel_width * 2);
          break;
        case band_horizontal:
          *visible_height = matrix_height / parallel_;
          *visible_width = (matrix_width * parallel_) - (panel_width * 2);
          break;
      }

      return true;
    }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int x, int y,
                                  int *matrix_x, int *matrix_y) const {

    const int panel_height = matrix_height / parallel_;
    const int panel_width = matrix_width / chain_;
    const int y_diff = int (x / matrix_width); //round down

    switch (mode_) {
      case normal:
        *matrix_x = x % matrix_width;
        *matrix_y = (y_diff * panel_height) + y;
        break;
      case band_vertical:
        *matrix_x = (x + panel_width) % matrix_width;
        *matrix_y = (int ((x + panel_width) / matrix_width) * panel_height) + y;
        break;
      case band_horizontal:
        *matrix_x = (x) % matrix_width;
        *matrix_y = (y_diff * panel_height) + y;
        break;
    }

  }

private:
  int chain_;
  int parallel_;
  Mode mode_;
};

// Rotate one or more panels by 0,90,180 or 270 degrees. 
// Parameter string example (rotate panel zero 90 degrees and panel two 180 degrees): Rotate-panel:0|90,2|180
class RotatePanelPixelMapper : public PixelMapper {
public:

  RotatePanelPixelMapper() {}

  virtual const char *GetName() const { return "Rotate-panel"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {

    chain_ = chain;
    parallel_ = parallel;

    if(param != NULL) {

      char* param_str = strdup(param);
      char* token1 = NULL; 
      char* token2 = NULL; 
      const char *param_delimiter_1 = ",";
      const char *param_delimiter_2 = "|"; 
      char* param_delimiter_1_ptr = NULL;
      char* param_delimiter_2_ptr = NULL;
      bool param_error = false;

      size_t str_index = 0;
      int i = 0;
      int panel_index = 0;
      int panel_angle = 0;

      const int panel_count = chain * parallel;

      //parse prameter string into map with panel id as index and rotation as value
      token1 = strtok_r(param_str, param_delimiter_1, &param_delimiter_1_ptr);
      while(token1 != NULL) {
        param_error = false;
        token2 = strtok_r(token1, param_delimiter_2, &param_delimiter_2_ptr);

        i=0;
        while(token2 != NULL) {
          //check that string is only number(s)
          for(str_index=0;str_index < strlen(token2);str_index++) {
            if(isdigit(token2[str_index]) == 0) {
              fprintf(stderr, "%s: error in parameter string, found non-digit: %s\n",this->GetName(), token2);
              param_error = true;
              break;
            }
          }

          if(param_error != false) {
            break;
          }

          if( i++ % 2 == 0) {
            panel_index = atoi(token2);
            if(panel_index > (panel_count -1)) {
              fprintf(stderr, "%s: error in parameter string, panel index is too high: %i (max: %i)\n",this->GetName(), panel_index,panel_count -1);
              break;
            }
          }else{
            panel_angle = atoi(token2);
            if ( panel_angle % 90 != 0) {
              fprintf(stderr, "%s: invalid parameter value for rotation: %i\n",this->GetName(), panel_angle);
            }else{
              panels_[panel_index] = panel_angle;
            }
          }

          token2 = strtok_r(NULL, param_delimiter_2,&param_delimiter_2_ptr);
        }

        token1 = strtok_r(NULL, param_delimiter_1,&param_delimiter_1_ptr);
      }
      free(param_str);
  
    }
    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height)
     const{

      *visible_width = matrix_width;
      *visible_height = matrix_height;

      return true;
  }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int x, int y,
                                  int *matrix_x, int *matrix_y) const {

    //calculate size of one panel
    static const int panel_cols_ = matrix_width / chain_;
    static const int panel_rows_ = matrix_height / parallel_;

    int angle = 0;

    //calculate on which panel we are
    const int panel_x_nr = int(x / (panel_cols_)); //panel count on x-axis. 0 or 1 for chain=2
    const int panel_y_nr = int(y / (panel_rows_)); //panel count on y-axis. 0,1 or 2 for parallel=3
    const int panel_nr = (panel_y_nr * chain_) + panel_x_nr;

    int panel_x = 0;
    int panel_y = 0;

    //check if we should do any rotation for the current panel
    if(panels_.count(panel_nr) > 0) {
      angle = panels_.at(panel_nr);

      //convert from x,y on the matrix to x,y on the panel
      panel_x = (panel_x_nr == 0) ? x : x % ((panel_x_nr) * panel_cols_);
      panel_y = (panel_y_nr == 0) ? y : y % ((panel_y_nr) * panel_rows_);

      switch (angle) {
      case 0:
        *matrix_x = x;
        *matrix_y = y;
        break;
      case 90:
        *matrix_x = (panel_x_nr * panel_cols_) + (panel_cols_ - panel_y - 1);
        *matrix_y = (panel_y_nr * panel_rows_) + panel_x;
        break;
      case 180:
        *matrix_x = (panel_x_nr * panel_cols_) + (panel_rows_ - panel_x - 1);
        *matrix_y = (panel_y_nr * panel_rows_) + (panel_cols_ - panel_y - 1);
        break;
      case 270:
        *matrix_x = (panel_x_nr * panel_cols_) + panel_y;
        *matrix_y = (panel_y_nr * panel_rows_) + (panel_rows_ - panel_x - 1);
        break;
      }
    }else{
      *matrix_x = x;
      *matrix_y = y;
    }

  }

private:
  int chain_;
  int parallel_;
  std::map<int,int> panels_;
};

// Reorder/change order of panels in setup.
// Parameter string example (change order of panels with index 1 and 3): Reorder:1|3,3|1
class ReorderPixelMapper : public PixelMapper {
public:

  ReorderPixelMapper() {}

  virtual const char *GetName() const { return "Reorder"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {

    chain_ = chain;
    parallel_ = parallel;

    if(param != NULL) {

      char* param_str = strdup(param);
      char* token1 = NULL; 
      char* token2 = NULL; 
      const char *param_delimiter_1 = ",";
      const char *param_delimiter_2 = "|"; 
      char* param_delimiter_1_ptr = NULL;
      char* param_delimiter_2_ptr = NULL;
      bool param_error = false;

      size_t str_index = 0;
      int i = 0;
      int panel_index_from = 0;
      int panel_index_to = 0;

      const int panel_count = chain * parallel;

      //parse prameter string into map with panel id as index and rotation as value
      token1 = strtok_r(param_str, param_delimiter_1, &param_delimiter_1_ptr);
      while(token1 != NULL) {
        param_error = false;
        token2 = strtok_r(token1, param_delimiter_2, &param_delimiter_2_ptr);

        i=0;
        while(token2 != NULL) {
          //check that string is only number(s)
          for(str_index=0;str_index < strlen(token2);str_index++) {
            if(isdigit(token2[str_index]) == 0) {
              fprintf(stderr, "%s: error in parameter string, found non-digit: %s\n",this->GetName(), token2);
              param_error = true;
              break;
            }
          }

          if(param_error != false) {
            break;
          }

          if( i++ % 2 == 0) {
            panel_index_from = atoi(token2);
            if(panel_index_from > (panel_count -1)) {
              fprintf(stderr, "%s: error in parameter string, panel index is too high: %i (max: %i)\n",this->GetName(), panel_index_from,panel_count -1);
              break;
            }
          }else{
            panel_index_to = atoi(token2);
            if(panel_index_to > (panel_count -1)) {
              fprintf(stderr, "%s: error in parameter string, panel index is too high: %i (max: %i)\n",this->GetName(), panel_index_to,panel_count -1);
              break;
            }

            panels_[panel_index_from] = panel_index_to;
          }

          token2 = strtok_r(NULL, param_delimiter_2,&param_delimiter_2_ptr);
        }

        token1 = strtok_r(NULL, param_delimiter_1,&param_delimiter_1_ptr);
      }
      free(param_str);
  
    }
    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height)
     const{

      *visible_width = matrix_width;
      *visible_height = matrix_height;

      return true;
  }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int x, int y,
                                  int *matrix_x, int *matrix_y) const {
    
    //calculate size of one panel
    static const int panel_cols_ = matrix_width / chain_;
    static const int panel_rows_ = matrix_height / parallel_;

    //calculate on which panel index x/y is
    const int panel_from_x_index = int(x / (panel_cols_)); //panel index on x-axis. 0 or 1 for chain=2
    const int panel_from_y_index = int(y / (panel_rows_)); //panel index on y-axis. 0,1 or 2 for parallel=3
    const int panel_from_index = (panel_from_y_index * chain_) + panel_from_x_index; //panel index in setup

    int panel_to_x_index = 0;
    int panel_to_y_index = 0;
    int panel_to_index = 0;

    int panel_x = 0;
    int panel_y = 0;

    //check if we should do any reordering for the current panel
    if(panels_.count(panel_from_index) > 0) {

      panel_to_index = panels_.at(panel_from_index);

      panel_to_x_index = panel_to_index % (parallel_ -1); //panel index on x-axis.
      panel_to_y_index = int(panel_to_index / (parallel_ -1)); //panel index on y-axis.

      //convert from x,y on the matrix to x,y on the current panel
      panel_x = (panel_from_x_index == 0) ? x : x % ((panel_from_x_index) * panel_cols_);
      panel_y = (panel_from_y_index == 0) ? y : y % ((panel_from_y_index) * panel_rows_);

      //calc x/y positions on the new panel
      *matrix_x = (panel_to_x_index * panel_cols_) + panel_x;
      *matrix_y = (panel_to_y_index * panel_rows_) + panel_y;

    }else{
      *matrix_x = x;
      *matrix_y = y;
    }

  }

private:
  int chain_;
  int parallel_;
  std::map<int,int> panels_;
};

class RotatePixelMapper : public PixelMapper {
public:
  RotatePixelMapper() : angle_(0) {}

  virtual const char *GetName() const { return "Rotate"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {
    if (param == NULL || strlen(param) == 0) {
      angle_ = 0;
      return true;
    }
    char *errpos;
    const int angle = strtol(param, &errpos, 10);
    if (*errpos != '\0') {
      fprintf(stderr, "Invalid rotate parameter '%s'\n", param);
      return false;
    }
    if (angle % 90 != 0) {
      fprintf(stderr, "Rotation needs to be multiple of 90 degrees\n");
      return false;
    }
    angle_ = (angle + 360) % 360;
    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height)
    const {
    if (angle_ % 180 == 0) {
      *visible_width = matrix_width;
      *visible_height = matrix_height;
    } else {
      *visible_width = matrix_height;
      *visible_height = matrix_width;
    }
    return true;
  }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int x, int y,
                                  int *matrix_x, int *matrix_y) const {
    switch (angle_) {
    case 0:
      *matrix_x = x;
      *matrix_y = y;
      break;
    case 90:
      *matrix_x = matrix_width - y - 1;
      *matrix_y = x;
      break;
    case 180:
      *matrix_x = matrix_width - x - 1;
      *matrix_y = matrix_height - y - 1;
      break;
    case 270:
      *matrix_x = y;
      *matrix_y = matrix_height - x - 1;
      break;
    }
  }

private:
  int angle_;
};

class MirrorPixelMapper : public PixelMapper {
public:
  MirrorPixelMapper() : horizontal_(true) {}

  virtual const char *GetName() const { return "Mirror"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {
    if (param == NULL || strlen(param) == 0) {
      horizontal_ = true;
      return true;
    }
    if (strlen(param) != 1) {
      fprintf(stderr, "Mirror parameter should be a single "
              "character:'V' or 'H'\n");
    }
    switch (*param) {
    case 'V':
    case 'v':
      horizontal_ = false;
      break;
    case 'H':
    case 'h':
      horizontal_ = true;
      break;
    default:
      fprintf(stderr, "Mirror parameter should be either 'V' or 'H'\n");
      return false;
    }
    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height)
    const {
    *visible_height = matrix_height;
    *visible_width = matrix_width;
    return true;
  }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int x, int y,
                                  int *matrix_x, int *matrix_y) const {
    if (horizontal_) {
      *matrix_x = matrix_width - 1 - x;
      *matrix_y = y;
    } else {
      *matrix_x = x;
      *matrix_y = matrix_height - 1 - y;
    }
  }

private:
  bool horizontal_;
};

// If we take a long chain of panels and arrange them in a U-shape, so
// that after half the panels we bend around and continue below. This way
// we have a panel that has double the height but only uses one chain.
// A single chain display with four 32x32 panels can then be arranged in this
// 64x64 display:
//    [<][<][<][<] }- Raspbery Pi connector
//
// can be arranged in this U-shape
//    [<][<] }----- Raspberry Pi connector
//    [>][>]
//
// This works for more than one chain as well. Here an arrangement with
// two chains with 8 panels each
//   [<][<][<][<]  }-- Pi connector #1
//   [>][>][>][>]
//   [<][<][<][<]  }--- Pi connector #2
//   [>][>][>][>]
class UArrangementMapper : public PixelMapper {
public:
  UArrangementMapper() : parallel_(1) {}

  virtual const char *GetName() const { return "U-mapper"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {
    if (chain < 2) {  // technically, a chain of 2 would work, but somewhat pointless
      fprintf(stderr, "U-mapper: need at least --led-chain=4 for useful folding\n");
      return false;
    }
    if (chain % 2 != 0) {
      fprintf(stderr, "U-mapper: Chain (--led-chain) needs to be divisible by two\n");
      return false;
    }
    parallel_ = parallel;
    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height)
    const {
    *visible_width = (matrix_width / 64) * 32;   // Div at 32px boundary
    *visible_height = 2 * matrix_height;
    if (matrix_height % parallel_ != 0) {
      fprintf(stderr, "%s For parallel=%d we would expect the height=%d "
              "to be divisible by %d ??\n",
              GetName(), parallel_, matrix_height, parallel_);
      return false;
    }
    return true;
  }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int x, int y,
                                  int *matrix_x, int *matrix_y) const {
    const int panel_height = matrix_height / parallel_;
    const int visible_width = (matrix_width / 64) * 32;
    const int slab_height = 2 * panel_height;   // one folded u-shape
    const int base_y = (y / slab_height) * panel_height;
    y %= slab_height;
    if (y < panel_height) {
      x += matrix_width / 2;
    } else {
      x = visible_width - x - 1;
      y = slab_height - y - 1;
    }
    *matrix_x = x;
    *matrix_y = base_y + y;
  }

private:
  int parallel_;
};



class VerticalMapper : public PixelMapper {
public:
  VerticalMapper() {}

  virtual const char *GetName() const { return "V-mapper"; }

  virtual bool SetParameters(int chain, int parallel, const char *param) {
    chain_ = chain;
    parallel_ = parallel;
    // optional argument :Z allow for every other panel to be flipped
    // upside down so that cabling can be shorter:
    // [ O < I ]   without Z       [ O < I  ]
    //   ,---^      <----                ^
    // [ O < I ]                   [ I > O  ]
    //   ,---^            with Z     ^
    // [ O < I ]            --->   [ O < I  ]
    z_ = (param && strcasecmp(param, "Z") == 0);
    return true;
  }

  virtual bool GetSizeMapping(int matrix_width, int matrix_height,
                              int *visible_width, int *visible_height)
    const {
    *visible_width = matrix_width * parallel_ / chain_;
    *visible_height = matrix_height * chain_ / parallel_;
#if 0
     fprintf(stderr, "%s: C:%d P:%d. Turning W:%d H:%d Physical "
	     "into W:%d H:%d Virtual\n",
             GetName(), chain_, parallel_,
	     *visible_width, *visible_height, matrix_width, matrix_height);
#endif
    return true;
  }

  virtual void MapVisibleToMatrix(int matrix_width, int matrix_height,
                                  int x, int y,
                                  int *matrix_x, int *matrix_y) const {
    const int panel_width  = matrix_width  / chain_;
    const int panel_height = matrix_height / parallel_;
    const int x_panel_start = y / panel_height * panel_width;
    const int y_panel_start = x / panel_width * panel_height;
    const int x_within_panel = x % panel_width;
    const int y_within_panel = y % panel_height;
    const bool needs_flipping = z_ && (y / panel_height) % 2 == 1;
    *matrix_x = x_panel_start + (needs_flipping
                                 ? panel_width - 1 - x_within_panel
                                 : x_within_panel);
    *matrix_y = y_panel_start + (needs_flipping
                                 ? panel_height - 1 - y_within_panel
                                 : y_within_panel);
  }

private:
  bool z_;
  int chain_;
  int parallel_;
};


typedef std::map<std::string, PixelMapper*> MapperByName;
static void RegisterPixelMapperInternal(MapperByName *registry,
                                        PixelMapper *mapper) {
  assert(mapper != NULL);
  std::string lower_name;
  for (const char *n = mapper->GetName(); *n; n++)
    lower_name.append(1, tolower(*n));
  (*registry)[lower_name] = mapper;
}

static MapperByName *CreateMapperMap() {
  MapperByName *result = new MapperByName();

  // Register all the default PixelMappers here.
  RegisterPixelMapperInternal(result, new RowArrangementMapper());
  RegisterPixelMapperInternal(result, new RotatePanelPixelMapper());
  RegisterPixelMapperInternal(result, new ReorderPixelMapper());
  RegisterPixelMapperInternal(result, new RotatePixelMapper());
  RegisterPixelMapperInternal(result, new UArrangementMapper());
  RegisterPixelMapperInternal(result, new VerticalMapper());
  RegisterPixelMapperInternal(result, new MirrorPixelMapper());
  return result;
}

static MapperByName *GetMapperMap() {
  static MapperByName *singleton_instance = CreateMapperMap();
  return singleton_instance;
}
}  // anonymous namespace

// Public API.
void RegisterPixelMapper(PixelMapper *mapper) {
  RegisterPixelMapperInternal(GetMapperMap(), mapper);
}

std::vector<std::string> GetAvailablePixelMappers() {
  std::vector<std::string> result;
  MapperByName *m = GetMapperMap();
  for (MapperByName::const_iterator it = m->begin(); it != m->end(); ++it) {
    result.push_back(it->second->GetName());
  }
  return result;
}

const PixelMapper *FindPixelMapper(const char *name,
                                   int chain, int parallel,
                                   const char *parameter) {
  std::string lower_name;
  for (const char *n = name; *n; n++) lower_name.append(1, tolower(*n));
  MapperByName::const_iterator found = GetMapperMap()->find(lower_name);
  if (found == GetMapperMap()->end()) {
    fprintf(stderr, "%s: no such mapper\n", name);
    return NULL;
  }
  PixelMapper *mapper = found->second;
  if (mapper == NULL) return NULL;  // should not happen.
  if (!mapper->SetParameters(chain, parallel, parameter))
    return NULL;   // Got parameter, but couldn't deal with it.
  return mapper;
}
}  // namespace rgb_matrix
