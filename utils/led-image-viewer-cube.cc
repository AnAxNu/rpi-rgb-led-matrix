#include "led-matrix.h"
#include "cube-canvas.h"
#include "pixel-mapper.h"
#include "content-streamer.h"

#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <Magick++.h>
#include <magick/image.h>

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

typedef int64_t tmillis_t;
static const tmillis_t distant_future = (1LL<<40); // that is a while.

struct ImageParams {
  ImageParams() : anim_duration_ms(distant_future), wait_ms(1500),
                  anim_delay_ms(-1), loops(-1), vsync_multiple(1) {}
  tmillis_t anim_duration_ms;  // If this is an animation, duration to show.
  tmillis_t wait_ms;           // Regular image: duration to show.
  tmillis_t anim_delay_ms;     // Animation delay override.
  int loops;
  int vsync_multiple;
};

struct FileInfo {
  ImageParams params;      // Each file might have specific timing settings
  std::vector<Magick::Image> image_sequence;
  std::size_t image_sequence_index;
  bool is_multi_frame;

};

static tmillis_t GetTimeInMillis() {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

static void SleepMillis(tmillis_t milli_seconds) {
  if (milli_seconds <= 0) return;
  struct timespec ts;
  ts.tv_sec = milli_seconds / 1000;
  ts.tv_nsec = (milli_seconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
}


// Load still image or animation.
// Scale, so that it fits in "width" and "height" and store in "result".
static bool LoadImageAndScale(const char *filename,
                              int target_width, int target_height,
                              bool fill_width, bool fill_height,
                              std::vector<Magick::Image> *result,
                              std::string *err_msg) {
  std::vector<Magick::Image> frames;
  try {
    readImages(&frames, filename);
  } catch (std::exception& e) {
    if (e.what()) *err_msg = e.what();
    return false;
  }
  if (frames.size() == 0) {
    fprintf(stderr, "No image found.");
    return false;
  }

  // Put together the animation from single frames. GIFs can have nasty
  // disposal modes, but they are handled nicely by coalesceImages()
  if (frames.size() > 1) {
    Magick::coalesceImages(result, frames.begin(), frames.end());
  } else {
    result->push_back(frames[0]);   // just a single still image.
  }

/*
  const int img_width = (*result)[0].columns();
  const int img_height = (*result)[0].rows();
  
  const float width_fraction = (float)target_width / img_width;
  const float height_fraction = (float)target_height / img_height;
  if (fill_width && fill_height) {
    // Scrolling diagonally. Fill as much as we can get in available space.
    // Largest scale fraction determines that.
    const float larger_fraction = (width_fraction > height_fraction)
      ? width_fraction
      : height_fraction;
    target_width = (int) roundf(larger_fraction * img_width);
    target_height = (int) roundf(larger_fraction * img_height);
  }
  else if (fill_height) {
    // Horizontal scrolling: Make things fit in vertical space.
    // While the height constraint stays the same, we can expand to full
    // width as we scroll along that axis.
    target_width = (int) roundf(height_fraction * img_width);
  }
  else if (fill_width) {
    // dito, vertical. Make things fit in horizontal space.
    target_height = (int) roundf(width_fraction * img_height);
  }
  */

  for (size_t i = 0; i < result->size(); ++i) {
    (*result)[i].scale(Magick::Geometry(target_width, target_height));
  }

  return true;
}

static bool DrawImageOnCanvas(const Magick::Image &img,rgb_matrix::FrameCanvas *canvas,int x_offset, int y_offset) {

  for (size_t y = 0; y < img.rows(); ++y) {
    for (size_t x = 0; x < img.columns(); ++x) {
      const Magick::Color &c = img.pixelColor(x, y);
      if (c.alphaQuantum() < 255) {
        canvas->SetPixel(x + x_offset, y + y_offset,
                          ScaleQuantumToChar(c.redQuantum()),
                          ScaleQuantumToChar(c.greenQuantum()),
                          ScaleQuantumToChar(c.blueQuantum()));
      }
    }
  }
  
  return true;
}

void DisplayAnimation(std::vector<FileInfo*> file_imgs, rgb_matrix::RGBMatrix *matrix, rgb_matrix::FrameCanvas *offscreen_canvas, int panel_width) {
  
  const tmillis_t override_anim_delay = file_imgs[0]->params.anim_delay_ms;

  std::size_t i = 0;
  int x_offset=0;
  int y_offset=0;
  FileInfo *p_file_info = NULL;
  Magick::Image *p_image = NULL;

  uint32_t delay_us = 130000;

  while (!interrupt_received) {

    const tmillis_t anim_delay_ms = override_anim_delay >= 0 ? override_anim_delay : delay_us / 1000;
    const tmillis_t start_wait_ms = GetTimeInMillis();

    for (i = 0;i < file_imgs.size();i++) {
      p_file_info = file_imgs[i];

      p_image = &p_file_info->image_sequence[p_file_info->image_sequence_index];

      //set offset
      x_offset = i * panel_width;
      
      //printf("i: %i, x_offset: %i, y_offset: %i \n",i, x_offset, y_offset);

      DrawImageOnCanvas(*p_image,offscreen_canvas,x_offset,y_offset);

      //reset index if last image
      if(++p_file_info->image_sequence_index == p_file_info->image_sequence.size()) {
        p_file_info->image_sequence_index = 0;
      }

    }

    offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas,1);
    
    const tmillis_t time_already_spent = GetTimeInMillis() - start_wait_ms;
//printf("SleepMillis : %lli \r\n",anim_delay_ms - time_already_spent);
    SleepMillis(anim_delay_ms - time_already_spent);
  }


}

int main(int argc, char *argv[]) {

  Magick::InitializeMagick(*argv);

  rgb_matrix::RGBMatrix::Options matrix_options;
  rgb_matrix::RuntimeOptions runtime_opt;
  // If started with 'sudo': make sure to drop privileges to same user
  // we started with, which is the most expected (and allows us to read
  // files as that user).
  runtime_opt.drop_priv_user = getenv("SUDO_UID");
  runtime_opt.drop_priv_group = getenv("SUDO_GID");
  if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv,&matrix_options, &runtime_opt)) {
    fprintf(stderr, "Failed to read runtime options\n");
    return -1;
  }


  // We remember ImageParams for each image
  std::map<const void *, struct ImageParams> filename_params;

  // Set defaults.
  ImageParams img_param;
  for (int i = 0; i < argc; ++i) {
    filename_params[argv[i]] = img_param;
  }

  const char *stream_output = NULL;

  // Prepare matrix
  runtime_opt.do_gpio_init = (stream_output == NULL);
  rgb_matrix::RGBMatrix *matrix = rgb_matrix::RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
  if (matrix == NULL) {
    fprintf(stderr, "Failed to create rgb matrix from options\n");
    return -1;
  }

  int panel_width = matrix_options.cols;

  rgb_matrix::FrameCanvas *offscreen_canvas = matrix->CreateFrameCanvas();

  //ununsed variables
  const bool fill_width = false;
  const bool fill_height = false;
  bool do_forever = false;

  const tmillis_t start_load = GetTimeInMillis();
  fprintf(stderr, "Loading %d files...\n", argc - optind);
  // Preparing all the images beforehand as the Pi might be too slow to
  // be quickly switching between these. So preprocess.
  std::vector<FileInfo*> file_imgs;
  int img_index = 0;
  for (int imgarg = optind; imgarg < argc; ++imgarg) {
    const char *filename = argv[imgarg];
    FileInfo *file_info = NULL;

    std::string err_msg;
    //std::vector<Magick::Image> image_sequence;
    file_info = new FileInfo();
    
    //load image/animations into vector
    if(LoadImageAndScale(filename, matrix_options.rows, matrix_options.cols,fill_width, fill_height, &file_info->image_sequence, &err_msg)) {

      file_info->image_sequence_index = 0;
      file_info->params = filename_params[filename];
      file_info->is_multi_frame = file_info->image_sequence.size() > 1;

      //calculate delay time
      for (size_t i = 0; i < file_info->image_sequence.size(); ++i) {
        const Magick::Image &img = file_info->image_sequence[i];
        int64_t delay_time_us;
        if (file_info->is_multi_frame) {
          //printf("img %i, multi-frame \r\n",img_index);
          delay_time_us = img.animationDelay() * 10000; // unit in 1/100s
        } else {
          //printf("img %i, inte multi-frame \r\n",img_index);
          delay_time_us = file_info->params.wait_ms * 1000;  // single image.
        }
        if (delay_time_us <= 0) delay_time_us = 100 * 1000;  // 1/10sec

        //printf("img %i, delay_time_us: %lli \r\n",img_index,delay_time_us);
      }

      file_imgs.push_back(file_info);
      img_index++;

    } else {
      // Ok, not an image.
      fprintf(stderr, "Failed to load file: %s\n",filename);
      continue;
    }

  }

  // Some parameter sanity adjustments.
  if (file_imgs.empty()) {
    // e.g. if all files could not be interpreted as image.
    fprintf(stderr, "No image could be loaded.\n");
    return 1;
  } else if (file_imgs.size() == 1) {
    // Single image: show forever.
    file_imgs[0]->params.wait_ms = distant_future;
  } else {
    for (size_t i = 0; i < file_imgs.size(); ++i) {
      file_imgs[i]->params.wait_ms = distant_future;
      //ImageParams &params = file_imgs[i]->params;
      // Forever animation ? Set to loop only once, otherwise that animation
      // would just run forever, stopping all the images after it.
      //if (params.loops < 0 && params.anim_duration_ms == distant_future) {
      //  params.loops = 1;
      //}
    }
  }

  fprintf(stderr, "Loading took %.3fs; now: Display.\n",(GetTimeInMillis() - start_load) / 1000.0);

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  do {
    DisplayAnimation(file_imgs, matrix, offscreen_canvas,panel_width);
  } while (do_forever && !interrupt_received);

  if (interrupt_received) {
    fprintf(stderr, "Caught signal. Exiting.\n");
  }

  // Animation finished. Shut down the RGB matrix.
  matrix->Clear();
  delete matrix;

  return 0;
}