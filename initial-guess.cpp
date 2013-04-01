#include "ht-api.h"
#include "ht-internal.h"
using namespace std;
using namespace cv;

#include "flandmark_detector.h"

typedef enum {
    fl_center = 0,
    fl_left_eye_int = 1,
    fl_right_eye_int = 2,
    fl_mouth_left = 3,
    fl_mouth_right = 4,
    fl_left_eye_ext = 5,
    fl_right_eye_ext = 6,
    fl_nose = 7,
    fl_count = 8
} fl_indices;

bool ht_fl_estimate(headtracker_t& ctx, Mat& frame, const Rect roi, Mat& rvec_, Mat& tvec_)
{
    int bbox[4];

    bbox[0] = roi.x;
    bbox[1] = roi.y;
    bbox[2] = roi.width + roi.x;
    bbox[3] = roi.height + roi.y;

    IplImage c_image = frame;

    double landmarks[fl_count * 2];

    if (flandmark_detect(&c_image, bbox, ctx.flandmark_model, landmarks))
        return false;

    Point2f left_eye_right = Point2f(
            landmarks[2 * fl_left_eye_int],
            landmarks[2 * fl_left_eye_int + 1]);
    Point2f left_eye_left = Point2f(
            landmarks[2 * fl_left_eye_ext],
            landmarks[2 * fl_left_eye_ext + 1]);
    Point2f right_eye_left = Point2f(
            landmarks[2 * fl_right_eye_int],
            landmarks[2 * fl_right_eye_int + 1]);
    Point2f right_eye_right = Point2f(
            landmarks[2 * fl_right_eye_ext],
            landmarks[2 * fl_right_eye_ext + 1]);
    Point2f nose = Point2f(landmarks[2 * fl_nose], landmarks[2 * fl_nose + 1]);
    Point2f mouth_left = Point2f(
            landmarks[2 * fl_mouth_left],
            landmarks[2 * fl_mouth_left + 1]);
    Point2f mouth_right = Point2f(
			landmarks[2 * fl_mouth_right],
			landmarks[2 * fl_mouth_right + 1]);

    vector<Point2f> image_points(7);
    vector<Point3f> object_points(7);

	if (!ctx.config.user_landmarks)
	{
		object_points[0] = Point3d(0, -0.002312, 0.10154);
		object_points[1] = Point3d(-0.01796, 0.03475, 0.08638);
		object_points[2] = Point3d(0.01796, 0.03475, 0.08638);
		object_points[3] = Point3d(-0.04810, 0.03560, 0.08034);
		object_points[4] = Point3d(0.04810, 0.03560, 0.08034);
		object_points[5] = Point3d(-0.02763, -0.03935, 0.09342);
		object_points[6] = Point3d(0.02763, -0.03935, 0.09342);

		for (int i = 0; i < object_points.size(); i++)
		{
			object_points[i].x *= 100;
			object_points[i].y *= 100;
			object_points[i].z *= 100;
		}
	} else {
		object_points[0] = Point3d(ctx.config.user_landmark_locations[0][0], ctx.config.user_landmark_locations[1][0], ctx.config.user_landmark_locations[2][0]);
		object_points[1] = Point3d(ctx.config.user_landmark_locations[0][1], ctx.config.user_landmark_locations[1][1], ctx.config.user_landmark_locations[2][1]);
		object_points[2] = Point3d(-ctx.config.user_landmark_locations[0][1], ctx.config.user_landmark_locations[1][1], ctx.config.user_landmark_locations[2][1]);
		object_points[3] = Point3d(ctx.config.user_landmark_locations[0][2], ctx.config.user_landmark_locations[1][2], ctx.config.user_landmark_locations[2][2]);
		object_points[4] = Point3d(-ctx.config.user_landmark_locations[0][2], ctx.config.user_landmark_locations[1][2], ctx.config.user_landmark_locations[2][2]);
		object_points[5] = Point3d(ctx.config.user_landmark_locations[0][3], ctx.config.user_landmark_locations[1][3], ctx.config.user_landmark_locations[2][3]);
		object_points[6] = Point3d(-ctx.config.user_landmark_locations[0][3], ctx.config.user_landmark_locations[1][3], ctx.config.user_landmark_locations[2][3]);

		if (ctx.config.debug)
			for (int i = 0; i < 6; i++)
				printf("%f %f %f\n", object_points[i].x, object_points[i].y, object_points[i].z);
	}

    image_points[0] = nose;
    image_points[1] = left_eye_right;
    image_points[2] = right_eye_left;
    image_points[3] = left_eye_left;
    image_points[4] = right_eye_right;
	image_points[5] = mouth_left;
	image_points[6] = mouth_right;

    Mat intrinsics = Mat::eye(3, 3, CV_32FC1);
    intrinsics.at<float> (0, 0) = ctx.focal_length_w;
    intrinsics.at<float> (1, 1) = ctx.focal_length_h;
    intrinsics.at<float> (0, 2) = ctx.grayscale.cols/2;
    intrinsics.at<float> (1, 2) = ctx.grayscale.rows/2;

    Mat dist_coeffs = Mat::zeros(5, 1, CV_32FC1);
    Mat rvec = Mat::zeros(3, 1, CV_64FC1);
    Mat tvec = Mat::zeros(3, 1, CV_64FC1);

    rvec.at<double> (0, 0) = 1.0;
    tvec.at<double> (0, 0) = 1.0;
    tvec.at<double> (1, 0) = 1.0;

    if (!solvePnP(object_points, image_points, intrinsics, dist_coeffs, rvec, tvec, false, HT_PNP_TYPE))
        return false;

#if 1
	if (ctx.bad_roi_count < 15 && ctx.has_pose && ctx.state == HT_STATE_TRACKING) {
		vector<Point2f> image_points2;
		vector<Point3f> object_points2(object_points.size());

		for (int i = 0; i < object_points.size(); i++)
			object_points2[i] = object_points[i];

		projectPoints(object_points2, ctx.rvec, ctx.tvec, intrinsics, dist_coeffs, image_points2);
#if 0
		float maxerr = ctx.zoom_ratio * ctx.config.ransac_max_reprojection_error * 2.5;
		maxerr *= maxerr;
		float total = 0;
		for (int i = 0; i < image_points.size(); i++)
		{
			float x2 = image_points[i].x - image_points2[i].x;
			float y2 = image_points[i].y - image_points2[i].y;
			x2 *= x2;
			y2 *= y2;
			if (x2 + y2 > maxerr) {
				ctx.bad_roi_count++;
				fprintf(stderr, "bad roi %f > %f\n", sqrt(x2 + y2), sqrt(maxerr));
				return false;
			}
			total += x2 + y2;
		}

		float maxerr2 = ctx.zoom_ratio * ctx.config.ransac_max_reprojection_error * 1.75;
		maxerr2 *= maxerr2;

		if (total / image_points.size() > maxerr2) {
			ctx.bad_roi_count++;
			fprintf(stderr, "bad roi %f > %f\n", sqrt(total / image_points.size()), sqrt(maxerr2));
			return false;
		}
#endif

		Scalar color(0, 0, 255);
		Scalar color2(0, 255, 0);
		for (int i = 0; i < image_points.size(); i++)
		{
			line(ctx.color, image_points[i], image_points2[i], color, 4);
			circle(ctx.color, image_points[i], 3, color2, -1);
		}
	}
#endif
	rvec_ = rvec;
    tvec_ = tvec;

	ctx.bad_roi_count = 0;

    return true;
}

bool ht_initial_guess(headtracker_t& ctx, Mat& frame, Mat& rvec_, Mat& tvec_) {
	int ticks = ht_tickcount();

	if (ctx.ticks_last_classification / ctx.config.classification_delay == ticks / ctx.config.classification_delay)
		return false;

	ctx.ticks_last_classification = ticks;

    Rect face_rect;

    if (!ht_classify(ctx.head_classifier, frame, face_rect))
        return false;

    return ht_fl_estimate(ctx, frame, face_rect, rvec_, tvec_);
}
