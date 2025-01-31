#include "GuidedFilter.h"

static cv::Mat boxfilter(const cv::Mat &I, int r)
{
    cv::Mat result;
    cv::blur(I, result, cv::Size(r, r));
    return result;
}

static cv::Mat boxfilter(const cv::Mat &I, const cv::Mat &mask, int r)
{
    CV_Assert(I.depth() == CV_32F);

    // assumes mask has data like (1: valid, 0: invalid)
    cv::Mat result, result_mask;
    cv::blur(I, result, cv::Size(r, r));
    cv::blur(mask, result_mask, cv::Size(r, r));

    for (int ri = 0; ri < result.rows; ri++)
        for (int ci = 0; ci < result.cols; ci++)
        {
            if (mask.at<float>(ri, ci) != 0)
            {
                result.at<float>(ri, ci) = result.at<float>(ri, ci) / result_mask.at<float>(ri, ci);
            }
            else
            {
                result.at<float>(ri, ci) = I.at<float>(ri, ci);
            }
        }

    return result;
}

static cv::Mat convertTo(const cv::Mat &mat, int depth)
{
    if (mat.depth() == depth)
        return mat;

    cv::Mat result;
    mat.convertTo(result, depth);
    return result;
}


cv::Mat GuidedFilterImpl::filter(const cv::Mat &p, int depth)
{
    cv::Mat p2 = convertTo(p, Idepth);

    cv::Mat result;
    if (p.channels() == 1)
    {
        result = filterSingleChannel(p2);
    }
    else
    {
        std::vector<cv::Mat> pc;
        cv::split(p2, pc);

        for (std::size_t i = 0; i < pc.size(); ++i)
            pc[i] = filterSingleChannel(pc[i]);

        cv::merge(pc, result);
    }

    return convertTo(result, depth == -1 ? p.depth() : depth);
}

GuidedFilterMono::GuidedFilterMono(const cv::Mat &origI, int r, double eps) : r(r), eps(eps)
{
    if (origI.depth() == CV_32F)
        I = origI.clone();
    else
        I = convertTo(origI, CV_32F);

    Idepth = I.depth();

    mean_I = boxfilter(I, r);
    cv::Mat mean_II = boxfilter(I.mul(I), r);
    var_I = mean_II - mean_I.mul(mean_I);
}

cv::Mat GuidedFilterMono::filterSingleChannel(const cv::Mat &p) const
{
    cv::Mat mean_p = boxfilter(p, r);
    cv::Mat mean_Ip = boxfilter(I.mul(p), r);
    cv::Mat cov_Ip = mean_Ip - mean_I.mul(mean_p); // this is the covariance of (I, p) in each local patch.

    cv::Mat a = cov_Ip / (var_I + eps); // Eqn. (5) in the paper;
    cv::Mat b = mean_p - a.mul(mean_I); // Eqn. (6) in the paper;

    cv::Mat mean_a = boxfilter(a, r);
    cv::Mat mean_b = boxfilter(b, r);
    return mean_a.mul(I) + mean_b;
}

GuidedFilterMonoWithMask::GuidedFilterMonoWithMask(const cv::Mat &origI, const cv::Mat &origM, int r, double eps) : r(r), eps(eps)
{
    if (origI.depth() == CV_32F)
        I = origI.clone();
    else
        I = convertTo(origI, CV_32F);

    if (origM.depth() == CV_32F)
        M = origM.clone();
    else
        M = convertTo(origM, CV_32F);

    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(M, &minVal, &maxVal, &minLoc, &maxLoc);
    M = M / maxVal;		// normalize to [0, 1]

    Idepth = I.depth();
    I = I.mul(M);

    mean_I = boxfilter(I, M, r);
    cv::Mat mean_II = boxfilter(I.mul(I), M, r);
    var_I = mean_II - mean_I.mul(mean_I);
}

cv::Mat GuidedFilterMonoWithMask::filterSingleChannel(const cv::Mat &p) const
{
    cv::Mat p_ = p.mul(M);
    cv::Mat mean_p = boxfilter(p_, M, r);
    cv::Mat mean_Ip = boxfilter(I.mul(p_), M, r);
    cv::Mat cov_Ip = mean_Ip - mean_I.mul(mean_p); // this is the covariance of (I, p) in each local patch.

    cv::Mat eps_mat = cv::Mat(mean_p.rows, mean_p.cols, CV_32F, cv::Scalar::all(eps));
    eps_mat = eps_mat.mul(M);
    eps_mat = boxfilter(eps_mat, r) + eps*0.00001;

    cv::Mat a = cov_Ip / (var_I + eps_mat); // Eqn. (5) in the paper;
    cv::Mat b = mean_p - a.mul(mean_I); // Eqn. (6) in the paper;

    cv::Mat mean_a = boxfilter(a, M, r);
    cv::Mat mean_b = boxfilter(b, M, r);
    return mean_a.mul(I) + mean_b;
}


GuidedFilterColor::GuidedFilterColor(const cv::Mat &origI, int r, double eps) : r(r), eps(eps)
{
    cv::Mat I;
    if (origI.depth() == CV_32F || origI.depth() == CV_64F)
        I = origI.clone();
    else
        I = convertTo(origI, CV_32F);

    Idepth = I.depth();

    cv::split(I, Ichannels);

    mean_I_r = boxfilter(Ichannels[0], r);
    mean_I_g = boxfilter(Ichannels[1], r);
    mean_I_b = boxfilter(Ichannels[2], r);

    // variance of I in each local patch: the matrix Sigma in Eqn (14).
    // Note the variance in each local patch is a 3x3 symmetric matrix:
    //           rr, rg, rb
    //   Sigma = rg, gg, gb
    //           rb, gb, bb
    cv::Mat var_I_rr = boxfilter(Ichannels[0].mul(Ichannels[0]), r) - mean_I_r.mul(mean_I_r) + eps;
    cv::Mat var_I_rg = boxfilter(Ichannels[0].mul(Ichannels[1]), r) - mean_I_r.mul(mean_I_g);
    cv::Mat var_I_rb = boxfilter(Ichannels[0].mul(Ichannels[2]), r) - mean_I_r.mul(mean_I_b);
    cv::Mat var_I_gg = boxfilter(Ichannels[1].mul(Ichannels[1]), r) - mean_I_g.mul(mean_I_g) + eps;
    cv::Mat var_I_gb = boxfilter(Ichannels[1].mul(Ichannels[2]), r) - mean_I_g.mul(mean_I_b);
    cv::Mat var_I_bb = boxfilter(Ichannels[2].mul(Ichannels[2]), r) - mean_I_b.mul(mean_I_b) + eps;

    // Inverse of Sigma + eps * I
    invrr = var_I_gg.mul(var_I_bb) - var_I_gb.mul(var_I_gb);
    invrg = var_I_gb.mul(var_I_rb) - var_I_rg.mul(var_I_bb);
    invrb = var_I_rg.mul(var_I_gb) - var_I_gg.mul(var_I_rb);
    invgg = var_I_rr.mul(var_I_bb) - var_I_rb.mul(var_I_rb);
    invgb = var_I_rb.mul(var_I_rg) - var_I_rr.mul(var_I_gb);
    invbb = var_I_rr.mul(var_I_gg) - var_I_rg.mul(var_I_rg);

    cv::Mat covDet = invrr.mul(var_I_rr) + invrg.mul(var_I_rg) + invrb.mul(var_I_rb);

    invrr /= covDet;
    invrg /= covDet;
    invrb /= covDet;
    invgg /= covDet;
    invgb /= covDet;
    invbb /= covDet;
}

cv::Mat GuidedFilterColor::filterSingleChannel(const cv::Mat &p) const
{
    cv::Mat mean_p = boxfilter(p, r);

    cv::Mat mean_Ip_r = boxfilter(Ichannels[0].mul(p), r);
    cv::Mat mean_Ip_g = boxfilter(Ichannels[1].mul(p), r);
    cv::Mat mean_Ip_b = boxfilter(Ichannels[2].mul(p), r);

    // covariance of (I, p) in each local patch.
    cv::Mat cov_Ip_r = mean_Ip_r - mean_I_r.mul(mean_p);
    cv::Mat cov_Ip_g = mean_Ip_g - mean_I_g.mul(mean_p);
    cv::Mat cov_Ip_b = mean_Ip_b - mean_I_b.mul(mean_p);

    cv::Mat a_r = invrr.mul(cov_Ip_r) + invrg.mul(cov_Ip_g) + invrb.mul(cov_Ip_b);
    cv::Mat a_g = invrg.mul(cov_Ip_r) + invgg.mul(cov_Ip_g) + invgb.mul(cov_Ip_b);
    cv::Mat a_b = invrb.mul(cov_Ip_r) + invgb.mul(cov_Ip_g) + invbb.mul(cov_Ip_b);

    cv::Mat b = mean_p - a_r.mul(mean_I_r) - a_g.mul(mean_I_g) - a_b.mul(mean_I_b); // Eqn. (15) in the paper;

    return (boxfilter(a_r, r).mul(Ichannels[0])
            + boxfilter(a_g, r).mul(Ichannels[1])
            + boxfilter(a_b, r).mul(Ichannels[2])
            + boxfilter(b, r));  // Eqn. (16) in the paper;
}

GuidedFilterColorWithMask::GuidedFilterColorWithMask(const cv::Mat &origI, const cv::Mat &origM, int r, double eps) : r(r), eps(eps)
{
    cv::Mat I;
    if (origI.depth() == CV_32F || origI.depth() == CV_64F)
        I = origI.clone();
    else
        I = convertTo(origI, CV_32F);

    if (origM.depth() == CV_32F)
        M = origM.clone();
    else
        M = convertTo(origM, CV_32F);

    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(M, &minVal, &maxVal, &minLoc, &maxLoc);
    M = M / maxVal;		// normalize to [0, 1]

    Idepth = I.depth();

    cv::split(I, Ichannels);
    Ichannels[0] = Ichannels[0].mul(M);
    Ichannels[1] = Ichannels[1].mul(M);
    Ichannels[2] = Ichannels[2].mul(M);

    mean_I_r = boxfilter(Ichannels[0], M, r);
    mean_I_g = boxfilter(Ichannels[1], M, r);
    mean_I_b = boxfilter(Ichannels[2], M, r);

    // variance of I in each local patch: the matrix Sigma in Eqn (14).
    // Note the variance in each local patch is a 3x3 symmetric matrix:
    //           rr, rg, rb
    //   Sigma = rg, gg, gb
    //           rb, gb, bb
    cv::Mat eps_mat = cv::Mat(mean_I_r.rows, mean_I_r.cols, CV_32F, cv::Scalar::all(eps));
    eps_mat = eps_mat.mul(M);
    eps_mat = boxfilter(eps_mat, r) + eps*0.00001;

    cv::Mat var_I_rr = boxfilter(Ichannels[0].mul(Ichannels[0]), M, r) - mean_I_r.mul(mean_I_r) + eps_mat;
    cv::Mat var_I_rg = boxfilter(Ichannels[0].mul(Ichannels[1]), M, r) - mean_I_r.mul(mean_I_g);
    cv::Mat var_I_rb = boxfilter(Ichannels[0].mul(Ichannels[2]), M, r) - mean_I_r.mul(mean_I_b);
    cv::Mat var_I_gg = boxfilter(Ichannels[1].mul(Ichannels[1]), M, r) - mean_I_g.mul(mean_I_g) + eps_mat;
    cv::Mat var_I_gb = boxfilter(Ichannels[1].mul(Ichannels[2]), M, r) - mean_I_g.mul(mean_I_b);
    cv::Mat var_I_bb = boxfilter(Ichannels[2].mul(Ichannels[2]), M, r) - mean_I_b.mul(mean_I_b) + eps_mat;

    // Inverse of Sigma + eps * I
    invrr = var_I_gg.mul(var_I_bb) - var_I_gb.mul(var_I_gb);
    invrg = var_I_gb.mul(var_I_rb) - var_I_rg.mul(var_I_bb);
    invrb = var_I_rg.mul(var_I_gb) - var_I_gg.mul(var_I_rb);
    invgg = var_I_rr.mul(var_I_bb) - var_I_rb.mul(var_I_rb);
    invgb = var_I_rb.mul(var_I_rg) - var_I_rr.mul(var_I_gb);
    invbb = var_I_rr.mul(var_I_gg) - var_I_rg.mul(var_I_rg);

    cv::Mat covDet = invrr.mul(var_I_rr) + invrg.mul(var_I_rg) + invrb.mul(var_I_rb);

    invrr /= covDet;
    invrg /= covDet;
    invrb /= covDet;
    invgg /= covDet;
    invgb /= covDet;
    invbb /= covDet;
}

cv::Mat GuidedFilterColorWithMask::filterSingleChannel(const cv::Mat &p) const
{
    cv::Mat p_ = p.mul(M);
    cv::Mat mean_p = boxfilter(p_, M, r);

    cv::Mat mean_Ip_r = boxfilter(Ichannels[0].mul(p_), M, r);
    cv::Mat mean_Ip_g = boxfilter(Ichannels[1].mul(p_), M, r);
    cv::Mat mean_Ip_b = boxfilter(Ichannels[2].mul(p_), M, r);

    // covariance of (I, p) in each local patch.
    cv::Mat cov_Ip_r = mean_Ip_r - mean_I_r.mul(mean_p);
    cv::Mat cov_Ip_g = mean_Ip_g - mean_I_g.mul(mean_p);
    cv::Mat cov_Ip_b = mean_Ip_b - mean_I_b.mul(mean_p);

    cv::Mat a_r = invrr.mul(cov_Ip_r) + invrg.mul(cov_Ip_g) + invrb.mul(cov_Ip_b);
    cv::Mat a_g = invrg.mul(cov_Ip_r) + invgg.mul(cov_Ip_g) + invgb.mul(cov_Ip_b);
    cv::Mat a_b = invrb.mul(cov_Ip_r) + invgb.mul(cov_Ip_g) + invbb.mul(cov_Ip_b);

    cv::Mat b = mean_p - a_r.mul(mean_I_r) - a_g.mul(mean_I_g) - a_b.mul(mean_I_b); // Eqn. (15) in the paper;

    return (boxfilter(a_r, M, r).mul(Ichannels[0])
            + boxfilter(a_g, M, r).mul(Ichannels[1])
            + boxfilter(a_b, M, r).mul(Ichannels[2])
            + boxfilter(b, M, r));  // Eqn. (16) in the paper;
}

GuidedFilter::GuidedFilter(const cv::Mat &I, int r, double eps)
{
    CV_Assert(I.channels() == 1 || I.channels() == 3);

    if (I.channels() == 1)
        impl_ = new GuidedFilterMono(I, 2 * r + 1, eps);
    else
        impl_ = new GuidedFilterColor(I, 2 * r + 1, eps);
}

GuidedFilter::GuidedFilter(const cv::Mat &I, const cv::Mat &M, int r, double eps)
{
    CV_Assert(I.channels() == 1 || I.channels() == 3);
    CV_Assert(M.channels() == 1);

    if (I.channels() == 1)
        impl_ = new GuidedFilterMonoWithMask(I, M, 2 * r + 1, eps);
    else
        impl_ = new GuidedFilterColorWithMask(I, M, 2 * r + 1, eps);
}

GuidedFilter::~GuidedFilter()
{
    delete impl_;
}

cv::Mat GuidedFilter::filter(const cv::Mat &p, int depth) const
{
    return impl_->filter(p, depth);
}

cv::Mat guidedFilter(const cv::Mat &I, const cv::Mat &p, int r, double eps, int depth)
{
    return GuidedFilter(I, r, eps).filter(p, depth);
}

cv::Mat guidedFilter(const cv::Mat &I, const cv::Mat &mask, const cv::Mat &p, int r, double eps, int depth)
{
    return GuidedFilter(I, mask, r, eps).filter(p, depth);
}