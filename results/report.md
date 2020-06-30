# Track an Object in 3D Space

## FP.1 Match 3D Objects

In current and previous frame, matches within the region of interest (ROI) were found and the keypoint occurances is incremented. After all matches were compared,best match was chosen based on the highest number of keypoint correspondences.

## FP.2 Compute Lidar-based TTC

- Only lidar points within the lane width of 4m were considered for the lidar based TTC. 
- Average was computed on the lidar points on both previous and current frame.
- TTC was computed using `TTC = avgXCurr * dT / (avgXPrev - avgXCurr)`

## FP.3 Associate Keypoint Correspondences with Bounding Boxes

1. The matches from the previous and the current frames were checked if it is enclosed by the bounding box ROI. 
2. Then the distance between the matched keypoints of previous and current frame was calculated
3. The distance value from step 2 added to the `distanceMean` variable and also `distanceCount` variable
4. Distance mean was found. 
5. Steps 1 and 2 were repeated
6. If the difference between the distance value from the step 2 and the distance mean is less than the distance threshold, then the match is assigned the given bounding box.

## FP.4 Compute Camera-based TTC

Across the previous and current frame, the distance the keypoints were calculated to find the distRatio between the distance from previous and distance from current frame. This distance is store in a vector to find a medianDistRatio in order to remove noisy outliers and then TTC to camera was found using: `TTC = -dT / (1 - medianDistRatio)`

## FP.5 Performance Evaluation 1

From the results.csv, it seems that TTC lidar always produces certain output irrespective of the number of detected keypoints.

## FP.5 Performance Evaluation 2

From the results.csv, it seems that TTC camera mostly produces undefined ouput. This could be due to less number of matched keypoints on the target object.

From the results.csv, it can been seen that the detector/descriptor combination of AKAZE/SIFT performed better and gives out some reliable result.
