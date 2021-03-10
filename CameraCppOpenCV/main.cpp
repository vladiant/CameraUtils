#include <cstddef>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <string>

int main(int argc, char* argv[]) {
  if (argc <= 1) {
    std::cout << "Format to call: " << argv[0]
              << " video_stream_file/camera_index" << '\n';
    return EXIT_FAILURE;
  }

  cv::VideoCapture cap;

  const std::string video_stream = argv[1];

  std::cout << "Opening video stream " << video_stream << '\n';

  if (!cap.open(video_stream)) {
    std::cout << "Error opening video stream: " << video_stream << '\n';
  }

  if (!cap.isOpened()) {
    const int camera_index = std::stoi(argv[1]);

    std::cout << "Opening camera index " << camera_index << '\n';

    if (!cap.open(camera_index)) {
      std::cout << "Error opening camera: " << camera_index << '\n';
      return EXIT_FAILURE;
    }
  }

  // Example to get/set camera parameters
  const double frame_width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
  const double frame_height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);

  std::cout << "Frame width: " << frame_width << '\n';
  std::cout << "Frame height: " << frame_height << '\n';

  const double new_frame_width = 800;
  std::cout << "Attempt to set frame width to: " << new_frame_width << " : ";
  std::cout << std::boolalpha
            << cap.set(cv::CAP_PROP_FRAME_WIDTH, new_frame_width) << '\n';

  const double new_frame_height = 600;
  std::cout << "Attempt to set frame height to: " << new_frame_height << " : ";
  std::cout << std::boolalpha
            << cap.set(cv::CAP_PROP_FRAME_HEIGHT, new_frame_height) << '\n';

  std::cout << "Current frame width: " << cap.get(cv::CAP_PROP_FRAME_WIDTH)
            << '\n';
  std::cout << "Current frame height: " << cap.get(cv::CAP_PROP_FRAME_HEIGHT)
            << '\n';

  const auto window_name = "Frame";
  cv::namedWindow(window_name, cv::WINDOW_NORMAL);

  cv::Mat frame;

  for (;;) {
    cap >> frame;

    if (frame.empty()) {
      break;
    }

    // Do processing here

    cv::imshow(window_name, frame);

    // Press ESC to leave
    if (cv::waitKey(10) == 27) {
      break;
    }
  }

  cap.release();

  std::cout << "Done.\n";

  return EXIT_SUCCESS;
}
