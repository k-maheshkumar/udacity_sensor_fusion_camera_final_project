
#include <iostream>
#include <algorithm>
#include <numeric>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "camFusion.hpp"
#include "dataStructures.h"

using namespace std;

// Create groups of Lidar points whose projection into the camera falls into the same bounding box
void clusterLidarWithROI(std::vector<BoundingBox> &boundingBoxes, std::vector<LidarPoint> &lidarPoints, float shrinkFactor, cv::Mat &P_rect_xx, cv::Mat &R_rect_xx, cv::Mat &RT)
{
    // loop over all Lidar points and associate them to a 2D bounding box
    cv::Mat X(4, 1, cv::DataType<double>::type);
    cv::Mat Y(3, 1, cv::DataType<double>::type);

    for (auto it1 = lidarPoints.begin(); it1 != lidarPoints.end(); ++it1)
    {
        // assemble vector for matrix-vector-multiplication
        X.at<double>(0, 0) = it1->x;
        X.at<double>(1, 0) = it1->y;
        X.at<double>(2, 0) = it1->z;
        X.at<double>(3, 0) = 1;

        // project Lidar point into camera
        Y = P_rect_xx * R_rect_xx * RT * X;
        cv::Point pt;
        pt.x = Y.at<double>(0, 0) / Y.at<double>(0, 2); // pixel coordinates
        pt.y = Y.at<double>(1, 0) / Y.at<double>(0, 2);

        vector<vector<BoundingBox>::iterator> enclosingBoxes; // pointers to all bounding boxes which enclose the current Lidar point
        for (vector<BoundingBox>::iterator it2 = boundingBoxes.begin(); it2 != boundingBoxes.end(); ++it2)
        {
            // shrink current bounding box slightly to avoid having too many outlier points around the edges
            cv::Rect smallerBox;
            smallerBox.x = (*it2).roi.x + shrinkFactor * (*it2).roi.width / 2.0;
            smallerBox.y = (*it2).roi.y + shrinkFactor * (*it2).roi.height / 2.0;
            smallerBox.width = (*it2).roi.width * (1 - shrinkFactor);
            smallerBox.height = (*it2).roi.height * (1 - shrinkFactor);

            // check wether point is within current bounding box
            if (smallerBox.contains(pt))
            {
                enclosingBoxes.push_back(it2);
            }

        } // eof loop over all bounding boxes

        // check wether point has been enclosed by one or by multiple boxes
        if (enclosingBoxes.size() == 1)
        {
            // add Lidar point to bounding box
            enclosingBoxes[0]->lidarPoints.push_back(*it1);
        }

    } // eof loop over all Lidar points
}

void show3DObjects(std::vector<BoundingBox> &boundingBoxes, cv::Size worldSize, cv::Size imageSize, bool bWait)
{
    // create topview image
    cv::Mat topviewImg(imageSize, CV_8UC3, cv::Scalar(255, 255, 255));

    for (auto it1 = boundingBoxes.begin(); it1 != boundingBoxes.end(); ++it1)
    {
        // create randomized color for current 3D object
        cv::RNG rng(it1->boxID);
        cv::Scalar currColor = cv::Scalar(rng.uniform(0, 150), rng.uniform(0, 150), rng.uniform(0, 150));

        // plot Lidar points into top view image
        int top = 1e8, left = 1e8, bottom = 0.0, right = 0.0;
        float xwmin = 1e8, ywmin = 1e8, ywmax = -1e8;
        for (auto it2 = it1->lidarPoints.begin(); it2 != it1->lidarPoints.end(); ++it2)
        {
            // world coordinates
            float xw = (*it2).x; // world position in m with x facing forward from sensor
            float yw = (*it2).y; // world position in m with y facing left from sensor
            xwmin = xwmin < xw ? xwmin : xw;
            ywmin = ywmin < yw ? ywmin : yw;
            ywmax = ywmax > yw ? ywmax : yw;

            // top-view coordinates
            int y = (-xw * imageSize.height / worldSize.height) + imageSize.height;
            int x = (-yw * imageSize.width / worldSize.width) + imageSize.width / 2;

            // find enclosing rectangle
            top = top < y ? top : y;
            left = left < x ? left : x;
            bottom = bottom > y ? bottom : y;
            right = right > x ? right : x;

            // draw individual point
            cv::circle(topviewImg, cv::Point(x, y), 4, currColor, -1);
        }

        // draw enclosing rectangle
        cv::rectangle(topviewImg, cv::Point(left, top), cv::Point(right, bottom), cv::Scalar(0, 0, 0), 2);

        // augment object with some key data
        char str1[200], str2[200];
        sprintf(str1, "id=%d, #pts=%d", it1->boxID, (int)it1->lidarPoints.size());
        putText(topviewImg, str1, cv::Point2f(left - 250, bottom + 50), cv::FONT_ITALIC, 2, currColor);
        sprintf(str2, "xmin=%2.2f m, yw=%2.2f m", xwmin, ywmax - ywmin);
        putText(topviewImg, str2, cv::Point2f(left - 250, bottom + 125), cv::FONT_ITALIC, 2, currColor);
    }

    // plot distance markers
    float lineSpacing = 2.0; // gap between distance markers
    int nMarkers = floor(worldSize.height / lineSpacing);
    for (size_t i = 0; i < nMarkers; ++i)
    {
        int y = (-(i * lineSpacing) * imageSize.height / worldSize.height) + imageSize.height;
        cv::line(topviewImg, cv::Point(0, y), cv::Point(imageSize.width, y), cv::Scalar(255, 0, 0));
    }

    cv::resize(topviewImg, topviewImg, cv::Size(imageSize.width / 2, imageSize.height / 2), 0, 0, cv::INTER_CUBIC);

    // display image
    string windowName = "3D Objects";
    cv::namedWindow(windowName, 1);
    cv::imshow(windowName, topviewImg);

    if (bWait)
    {
        cv::waitKey(0); // wait for key to be pressed
    }
}

// associate a given bounding box with the keypoints it contains
void clusterKptMatchesWithROI(BoundingBox &boundingBox, std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, std::vector<cv::DMatch> &kptMatches)
{
    // assert(kptsPrev.size() == kptsCurr.size());

    int distanceCount = 0;
    float distanceMean = 0.0;
    float distanceThreshold = 0.1;

    for (cv::DMatch match : kptMatches)
    {
        cv::KeyPoint previousKeypt = kptsPrev.at(match.queryIdx);
        cv::KeyPoint currentKeypt = kptsCurr.at(match.trainIdx);

        if (boundingBox.roi.contains(currentKeypt.pt))
        {
            distanceMean += cv::norm(currentKeypt.pt - previousKeypt.pt);
            distanceCount++;
        }
    }

    distanceMean /= distanceCount;

    for (cv::DMatch match : kptMatches)
    {
        cv::KeyPoint previousKeypt = kptsPrev.at(match.queryIdx);
        cv::KeyPoint currentKeypt = kptsCurr.at(match.trainIdx);

        if (boundingBox.roi.contains(currentKeypt.pt))
        {
            float distance = cv::norm(currentKeypt.pt - previousKeypt.pt);

            if (abs(distance - distanceMean) < distanceThreshold)
            {
                boundingBox.kptMatches.push_back(match);
                boundingBox.keypoints.push_back(currentKeypt);
            }
        }
    }
}

double calcMedian(vector<double> &distRatios)
{
    std::sort(distRatios.begin(), distRatios.end());

    int middleIndex = distRatios.size() / 2;

    double median = distRatios.size() % 2 ? distRatios[middleIndex] : (distRatios[middleIndex] + distRatios[middleIndex + 1]) / 2;

    return median;
}

// Compute time-to-collision (TTC) based on keypoint correspondences in successive images
void computeTTCCamera(std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr,
                      std::vector<cv::DMatch> kptMatches, double frameRate, double &TTC, cv::Mat *visImg)
{
    double minDist = 1.0; // min. required distance

    // compute distance ratios between all matched keypoints
    vector<double> distRatios; // stores the distance ratios for all keypoints between curr. and prev. frame
    for (cv::DMatch outerMatch : kptMatches)
    {
        cv::KeyPoint kpOuterCurr = kptsCurr.at(outerMatch.trainIdx);
        cv::KeyPoint kpOuterPrev = kptsPrev.at(outerMatch.queryIdx);

        for (cv::DMatch innerMatch : kptMatches)
        {
            // get next keypoint and its matched partner in the prev. frame
            cv::KeyPoint kpInnerCurr = kptsCurr.at(innerMatch.trainIdx);
            cv::KeyPoint kpInnerPrev = kptsPrev.at(innerMatch.queryIdx);

            // compute distances and distance ratios
            double distCurr = cv::norm(kpOuterCurr.pt - kpInnerCurr.pt);
            double distPrev = cv::norm(kpOuterPrev.pt - kpInnerPrev.pt);

            // avoid division by zero
            if (distPrev > std::numeric_limits<double>::epsilon() && distCurr >= minDist)
            {

                double distRatio = distCurr / distPrev;
                distRatios.push_back(distRatio);
            }
        }
    }

    // only continue if list of distance ratios is not empty
    if (distRatios.size() == 0)
    {
        TTC = NAN;
        return;
    }

    double medianDistRatio = calcMedian(distRatios);

    double dT = 1 / frameRate;
    TTC = -dT / (1 - medianDistRatio);
}

void computeTTCLidar(std::vector<LidarPoint> &lidarPointsPrev,
                     std::vector<LidarPoint> &lidarPointsCurr, double frameRate, double &TTC)
{
    TTC = 0;
    double dT = 1 / frameRate; // time between two measurements in seconds
    int minX = 0, maxX = 10;
    double laneWidth = 4.0; // assumed width of the ego lane

    // find avergae closest distance to Lidar points within ego lane
    double avgXPrev = 0, avgXCurr = 0;
    double avgXPrevCount = 0, avgXCurrCount = 0;

    for (auto lidarPoint : lidarPointsPrev)
    {

        if (lidarPoint.x > minX && lidarPoint.x < maxX && fabs(lidarPoint.y) <= laneWidth / 2)
        {
            avgXPrev += lidarPoint.x;
            avgXPrevCount += 1;
        }
    }

    for (auto lidarPoint : lidarPointsCurr)
    {

        if (lidarPoint.x > minX && lidarPoint.x < maxX && fabs(lidarPoint.y) <= laneWidth / 2)
        {
            avgXCurr += lidarPoint.x;
            avgXCurrCount += 1;
        }
    }

    if (avgXPrevCount && avgXCurrCount)
    {
        avgXPrev /= avgXPrevCount;
        avgXCurr /= avgXCurrCount;

        TTC = avgXCurr * dT / (avgXPrev - avgXCurr);
    }
}

void matchBoundingBoxes(std::vector<cv::DMatch> &matches, std::map<int, int> &bbBestMatches, DataFrame &prevFrame, DataFrame &currFrame)
{
    vector<vector<int>> matchCount(prevFrame.boundingBoxes.size(), vector<int>(currFrame.boundingBoxes.size(), 0));
    double shrinkFactor = 0.10;

    for (cv::DMatch match : matches)
    {
        cv::KeyPoint previousKeypt = prevFrame.keypoints.at(match.queryIdx);
        cv::KeyPoint currentKeypt = currFrame.keypoints.at(match.trainIdx);

        int prevBoxIndex = 0;
        int currBoxIndex = 0;

        vector<int> prevBbIndices;
        vector<int> currBbIndices;

        for (BoundingBox bbox : prevFrame.boundingBoxes)
        {
            if (bbox.roi.contains(previousKeypt.pt))
            {
                prevBbIndices.push_back(prevBoxIndex);
            }

            prevBoxIndex++;
        }

        for (BoundingBox bbox : currFrame.boundingBoxes)
        {
            if (bbox.roi.contains(currentKeypt.pt))
            {
                currBbIndices.push_back(currBoxIndex);
            }

            currBoxIndex++;
        }

        if (prevBbIndices.size() && currBbIndices.size())
        {
            for (int prevIndx : prevBbIndices)
            {
                for (int currIndx : currBbIndices)
                {
                    matchCount[prevIndx][currIndx] += 1;
                }
            }
        }
    }

    for (int prevIndx = 0; prevIndx < prevFrame.boundingBoxes.size(); prevIndx++)
    {
        int bestMatchIndex = 0;
        int maxScore = 0;

        for (int currIndx = 0; currIndx < currFrame.boundingBoxes.size(); currIndx++)
        {
            if (maxScore < matchCount[prevIndx][currIndx])
            {
                maxScore = matchCount[prevIndx][currIndx];
                bestMatchIndex = currIndx;
            }
        }

        bbBestMatches[prevIndx] = bestMatchIndex;
    }
}
