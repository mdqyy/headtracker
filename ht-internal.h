// todo do away with leaks if initialization fails
#pragma once
#include <vector>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <math.h>
#include <opencv2/opencv.hpp>
#include <opencv/highgui.h>
#include "ht-api.h"
#include "flandmark_detector.h"
using namespace std;
using namespace cv;
#define HT_PI 3.1415926535

#define HT_PNP_TYPE ITERATIVE

typedef enum {
	HT_STATE_INITIALIZING = 0, // waiting for RANSAC consensus
	HT_STATE_TRACKING = 1, // ransac consensus established
    HT_STATE_LOST = 2 // consensus lost; fall back to initializing
} state_t;

typedef struct {
    Point3f p1;
    Point3f p2;
    Point3f p3;
} triangle_t;

typedef struct {
    Point2f p1;
    Point2f p2;
    Point2f p3;
} triangle2d_t;

typedef struct {
	triangle_t* triangles;
	triangle2d_t* projection;
	int count;
} model_t;

static __inline int ht_tickcount(void) {
	return (int) (cv::getTickCount() * 1000 / cv::getTickFrequency());
}

typedef struct {
	int idx;
    Point2f position;
} ht_keypoint;

typedef struct ht_context {
    float focal_length_w;
    float focal_length_h;
    VideoCapture camera;
    Mat grayscale, color;
    CascadeClassifier head_classifier;
	int ticks_last_classification;
	int ticks_last_features;
	model_t model;
	model_t bbox;
	state_t state;
    vector<Mat>* pyr_a;
    vector<Mat>* pyr_b;
	bool restarted;
	float zoom_ratio;
	ht_config_t config;
	ht_keypoint* keypoints;
    Point3f* keypoint_uv;
    int ticks_last_second;
    int hz;
    int hz_last_second;
    FLANDMARK_Model* flandmark_model;
    int ticks_last_flandmark;
	Mat rvec, tvec;
	bool has_pose;
    int fast_state;
} headtracker_t;

model_t ht_load_model(const char* filename);
bool ht_point_inside_triangle_2d(const Point2d p1, const Point2d p2, const Point2d p3, const Point2d px, Point2f& uv);

bool ht_classify(CascadeClassifier& classifier, Mat& frame, Rect& ret);

bool ht_get_image(headtracker_t& ctx);

bool ht_initial_guess(headtracker_t& ctx, Mat& frame, Mat &rvec, Mat &tvec);
bool ht_project_model(headtracker_t& ctx,
                      const Mat& rvec,
                      const Mat& tvec,
                      model_t& model);
bool ht_triangle_at(const Point2f pos, triangle_t* ret, int* idx, const model_t& model, Point2f &uv);
void ht_draw_model(headtracker_t& ctx, model_t& model);
void ht_get_features(headtracker_t& ctx, model_t& model);
void ht_track_features(headtracker_t& ctx);
void ht_draw_features(headtracker_t& ctx);

static __inline float ht_distance2d_squared(const Point2f p1, const Point2f p2) {
	float x = p1.x - p2.x;
	float y = p1.y - p2.y;
	return x * x + y * y;
}

bool ht_ransac_best_indices(headtracker_t& ctx, float& mean_error, Mat& rvec, Mat& tvec);
Point3f ht_get_triangle_pos(const Point2f uv, const triangle_t& t);
Rect ht_get_bounds(headtracker_t& ctx, model_t& model);
bool ht_fl_estimate(headtracker_t& ctx, Mat& frame, const Rect roi, Mat& rvec_, Mat& tvec_);
