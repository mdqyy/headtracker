#include "stdafx.h"

using namespace std;
using namespace cv;

static double ht_avg_reprojection_error(headtracker_t& ctx,
                                         CvPoint3D32f* model_points,
                                         CvPoint2D32f* image_points,
                                         int point_cnt,
                                         CvPOSITObject** prev_pObject,
                                         float* rotation_matrix,
                                         float* translation_vector) {
    double focal_length = ctx.focal_length;

    CvPOSITObject* posit_obj = cvCreatePOSITObject(model_points, point_cnt);
    if (*prev_pObject)
        cvReleasePOSITObject(prev_pObject);
    *prev_pObject = posit_obj;
    cvPOSIT(posit_obj,
            image_points,
            focal_length,
            cvTermCriteria(CV_TERMCRIT_EPS | CV_TERMCRIT_ITER, ctx.config.ransac_posit_iter, ctx.config.ransac_posit_eps),
            rotation_matrix,
            translation_vector);

    double ret = 0;
    for (int i = 0; i < point_cnt; i++) {
        float foo = ht_distance2d_squared(ht_project_point(model_points[i], rotation_matrix, translation_vector, ctx.focal_length), image_points[i]);
        if (foo > ret)
            ret = foo;
    }
    return sqrt(ret / point_cnt);
}

void ht_fisher_yates(int* indices, int count) {
    int tmp;

    for (int i = count - 1; i > 0; i--) {
        int j = rand() % i;
        tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }
}

bool ht_ransac(headtracker_t& ctx,
               float max_error,
               int* best_feature_cnt,
               int* best_keypoint_cnt,
               double* best_error,
               int* best_indices,
               int* best_keypoints,
               float error_scale)
{
    if (ctx.keypoint_count == 0)
        return false;

    int mcnt = ctx.model.count;
    int k = 0;
    bool ret = false;
    int* keypoint_indices = new int[ctx.keypoint_count];
    int* model_feature_indices = new int[mcnt];
    int* model_keypoint_indices = new int[ctx.keypoint_count];
    int* indices = new int[mcnt];
    const float bias = ctx.config.ransac_smaller_error_preference;
    const int N = 4;
    const int K = ctx.config.ransac_num_iters;

    *best_error = 1.0e10;
    *best_feature_cnt = 0;
    *best_keypoint_cnt = 0;

    for (int i = 0; i < mcnt; i++) {
        if (ctx.features[i].x != -1)
            indices[k++] = i;
    }

    int kppos = 0;

    for (int i = 0; i < ctx.keypoint_count; i++) {
        if (ctx.keypoints[i].idx != -1)
            keypoint_indices[kppos++] = i;
    }

    CvPoint2D32f* image_points = new CvPoint2D32f[k + kppos];
    CvPoint3D32f* model_points = new CvPoint3D32f[k + kppos];

    if ((k + kppos) < N || (k + kppos) < 4)
        goto end;

    for (int iter = 0; iter < K; iter++) {
        ht_fisher_yates(indices, k);
        ht_fisher_yates(keypoint_indices, kppos);
        int ipos = 0;
        int fpos = 0;
        int kpos = 0;
        int gfpos = 0;
        int gkpos = 0;
        float rotation_matrix[9];
        float translation_vector[3];
        float rotation_new[9];
        float translation_new[3];

        memset(rotation_matrix, 0, sizeof(float) * 9);
        memset(translation_vector, 0, sizeof(float) * 3);

        CvPoint3D32f first_point = ctx.feature_uv[indices[0]];

        double cur_error;
        cur_error = 1.0e10;

        CvPOSITObject* posit_obj = NULL;

        for (; fpos < k; fpos++) {
            int idx = indices[fpos];
            model_points[ipos] = ctx.feature_uv[idx];
            model_points[ipos].x -= first_point.x;
            model_points[ipos].y -= first_point.y;
            model_points[ipos].z -= first_point.z;
            image_points[ipos] = ctx.features[idx];
            model_feature_indices[gfpos] = idx;

            if (ipos >= N) {
                memcpy(rotation_new, rotation_matrix, sizeof(float) * 9);
                memcpy(translation_new, translation_vector, sizeof(float) * 3);
                double e = ht_avg_reprojection_error(ctx, model_points, image_points, ipos+1, &posit_obj, rotation_new, translation_new);
                e *= error_scale;

                if (e*max_error > cur_error)
                    continue;
                cur_error = e;

                memcpy(rotation_matrix, rotation_new, sizeof(float) * 9);
                memcpy(translation_vector, translation_new, sizeof(float) * 3);
            }

            ipos++;
            gfpos++;

            if (ipos >= N && ipos * ((1.0f - bias) + bias * (*best_error / cur_error)) > *best_feature_cnt + *best_keypoint_cnt)
            {
                ret = true;
                *best_error = cur_error;
                *best_feature_cnt = gfpos;
                *best_keypoint_cnt = gkpos;
                for (int i = 0; i < gfpos; i++)
                    best_indices[i] = model_feature_indices[i];
                for (int i = 0; i < gkpos; i++)
                    best_keypoints[i] = model_keypoint_indices[i];
            }
        }

        for (; kpos < kppos; kpos++) {
            int idx = keypoint_indices[kpos];
            ht_keypoint& kp = ctx.keypoints[idx];
            model_points[ipos] = ctx.keypoint_uv[idx];
            model_points[ipos].x -= first_point.x;
            model_points[ipos].y -= first_point.y;
            model_points[ipos].z -= first_point.z;
            image_points[ipos] = kp.position;
            model_keypoint_indices[gkpos] = idx;

            if (ipos >= N) {
                memcpy(rotation_new, rotation_matrix, sizeof(float) * 9);
                memcpy(translation_new, translation_vector, sizeof(float) * 3);
                double e = ht_avg_reprojection_error(ctx, model_points, image_points, ipos+1, &posit_obj, rotation_new, translation_new);
                e *= error_scale;
                if (e*max_error > cur_error)
                    continue;
                cur_error = e;
                memcpy(rotation_matrix, rotation_new, sizeof(float) * 9);
                memcpy(translation_vector, translation_new, sizeof(float) * 3);
            }

            ipos++;
            gkpos++;

            if (ipos >= N && ipos * ((1.0f - bias) + bias * (*best_error / cur_error)) > *best_feature_cnt + *best_keypoint_cnt)
            {
                ret = true;
                *best_error = cur_error;
                *best_feature_cnt = gfpos;
                *best_keypoint_cnt = gkpos;
                for (int i = 0; i < gfpos; i++)
                    best_indices[i] = model_feature_indices[i];
                for (int i = 0; i < gkpos; i++)
                    best_keypoints[i] = model_keypoint_indices[i];
            }
        }
        if (posit_obj)
            cvReleasePOSITObject(&posit_obj);
    }

end:

    delete[] keypoint_indices;
    delete[] indices;
    delete[] image_points;
    delete[] model_points;
    delete[] model_feature_indices;
    delete[] model_keypoint_indices;

    if (!ret && ctx.abortp)
        abort();

    return ret;
}

bool ht_ransac_best_indices(headtracker_t& ctx, double* best_error) {
    int* best_feature_indices = new int[ctx.feature_count];
    int* best_keypoint_indices = new int[ctx.keypoint_count];
    bool goodp = false;
    double max_error = ctx.config.ransac_max_error;
    double really_best_error = 1.0e10f;
#if 0
    int last_cnt_f = -1;
    int last_cnt_k = -1;
start:
#endif
    int best_feature_cnt, best_keypoint_cnt;
    if (ht_ransac(ctx,
                  max_error,
                  &best_feature_cnt,
                  &best_keypoint_cnt,
                  best_error,
                  best_feature_indices,
                  best_keypoint_indices,
                  ctx.zoom_ratio) &&
        best_feature_cnt + best_keypoint_cnt > ctx.config.ransac_min_features)
    {
        goodp = true;
        char* fusedp = new char[ctx.model.count];
        char* kusedp = new char[ctx.config.max_keypoints];
        for (int i = 0; i < ctx.model.count; i++)
            fusedp[i] = 0;
        for (int i = 0; i < best_feature_cnt; i++) {
            fusedp[best_feature_indices[i]] = 1;
        }
        for (int i = 0; i < ctx.model.count; i++) {
            if (!fusedp[i]) {
                if (ctx.features[i].x != -1) {
                    if (++ctx.feature_failed_iters[i] > ctx.config.feature_max_failed_ransac) {
                        ctx.features[i].x = -1;
                        ctx.feature_count--;
                    }
                }
            } else {
                ctx.feature_failed_iters[i] = 0;
            }
        }
        for (int i = 0; i < ctx.config.max_keypoints; i++)
            kusedp[i] = 0;
        for (int i = 0; i < best_keypoint_cnt; i++)
            kusedp[best_keypoint_indices[i]] = 1;
        for (int i = 0; i < ctx.config.max_keypoints; i++) {
            if (!kusedp[i] && ctx.keypoints[i].idx != -1) {
                if (++ctx.keypoint_failed_iters[i] > ctx.config.keypoint_max_failed_ransac) {
                    ctx.keypoints[i].idx = -1;
                    ctx.keypoint_count--;
                }
            } else {
                ctx.keypoint_failed_iters[i] = 0;
            }
        }
        delete[] fusedp;
        delete[] kusedp;
#if 0
        really_best_error = *best_error;
        max_error = really_best_error -  0.01;

        if (max_error > 1.0e-2 && (best_keypoint_cnt != last_cnt_k || best_feature_cnt != last_cnt_f)) {
            last_cnt_f = best_feature_cnt;
            last_cnt_k = best_keypoint_cnt;
            goto start;
        }
#endif

        delete[] best_keypoint_indices;
        delete[] best_feature_indices;
        return true;
    }
    delete[] best_keypoint_indices;
    delete[] best_feature_indices;
    *best_error = really_best_error;
    return goodp;
}