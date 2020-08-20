import sys
import os

import cv2


if len(sys.argv) <= 1:
    print(f"Format to call: {sys.argv[0]} video_stream_file/camera_index")
    exit(os.EX_IOERR)

video_stream = sys.argv[1]

print(f"Opening video stream {video_stream}")

cap = cv2.VideoCapture(video_stream)

if not cap.isOpened():
    print(f"Error opening video stream: {video_stream}")

    camera_index = int(sys.argv[1])
    print(f"Opening camera index {camera_index}")
    cap = cv2.VideoCapture(camera_index)

    if not cap.isOpened():
        print(f"Error opening camera index {camera_index}")
        exit(os.EX_IOERR)

frame_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
frame_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

print(f"Frame width: {frame_width}")
print(f"Frame height: {frame_height}")

new_frame_width = 800
set_width_status = cap.set(cv2.CAP_PROP_FRAME_WIDTH, new_frame_width)
print(f"Attempt to set frame width to: {new_frame_width} - {set_width_status}")

new_frame_height = 600
set_height_status = cap.set(cv2.CAP_PROP_FRAME_HEIGHT, new_frame_height)
print(f"Attempt to set frame height to: {new_frame_height} - {set_height_status}")

frame_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
frame_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

print(f"New frame width: {frame_width}")
print(f"New frame height: {frame_height}")

cv2.namedWindow("Frame", cv2.WINDOW_NORMAL)

while True:
    ret, frame = cap.read()
    if not ret:
        break

    cv2.imshow("Frame", frame)

    if cv2.waitKey(10) & 0xFF == 27:
        break

cap.release()

print("Done.")

cv2.destroyAllWindows()
