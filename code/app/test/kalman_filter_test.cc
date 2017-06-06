#include <gtest/gtest.h>

#include "amdados/app/static_grid.h"
#include "amdados/app/utils/common.h"
#include "amdados/app/utils/amdados_utils.h"
#include "amdados/app/utils/matrix.h"
#include "amdados/app/utils/cholesky.h"
#include "amdados/app/utils/configuration.h"
#include "amdados/app/model/i_model.h"
#include "amdados/app/utils/kalman_filter.h"

using namespace amdados::app::utils;

static Configuration gConf;         // a single global configuration

//-------------------------------------------------------------------------------------------------
// Function for testing a Kalman filter on a toy 2D problem.
//-------------------------------------------------------------------------------------------------
TEST(KalmanFilter, Basic)
{
    // Read configuration settings.
    gConf.ReadConfigFile("../../amdados_unittest.conf");
    gConf.PrintParameters();
    MakeDirectory(gConf.asCString("output_dir"));

    // Open a file for printing summary of the test.
    std::string summaryFileName = gConf.asString("output_dir") + "/kalman_summary_test.log";
    std::fstream summaryFile(summaryFileName, std::ios::out | std::ios::trunc);
    assert_true(summaryFile.good())
        << "failed to oped the summary file: " << summaryFileName << std::endl;

	// Open a file for storing the results of particle path tracking by the Kalman filter.
    std::string trackFileName = gConf.asString("output_dir") + "/kalman_filter_test.out";
	std::fstream trackFile(trackFileName, std::ios::out | std::ios::trunc);
    assert_true(trackFile.good())
        << "failed to oped the track file: " << trackFileName << std::endl;

	const size_t DIM = 3;
	const int    NUM_TIME_STEPS = 5000;
	const double DEVIATION_MODEL = 1.0;
	const double DEVIATION_MEASUREMENT = 1.0;
	const char   SPACE[] = " ";

	using matrix_t     = KalmanFilter<DIM,DIM>::matrix_t;
	using matrix_MxN_t = KalmanFilter<DIM,DIM>::matrix_MxN_t;
	using matrix_MxM_t = KalmanFilter<DIM,DIM>::matrix_MxM_t;
	using vector_t     = KalmanFilter<DIM,DIM>::vector_t;
	using vector_obs_t = KalmanFilter<DIM,DIM>::vector_obs_t;

	// Gaussian noise generator.
    std::mt19937               gen(std::time(nullptr));
	std::normal_distribution<> distrib_v(0.0, DEVIATION_MEASUREMENT);

	vector_t     x0;    FillVector(x0, 0.0);
	matrix_t     P0;    MakeIdentityMatrix(P0);     MatScalarMult(P0, DEVIATION_MODEL);
	matrix_t     A;     MakeIdentityMatrix(A);
	matrix_t     Q;     MakeIdentityMatrix(Q);      MatScalarMult(Q, DEVIATION_MODEL);
	matrix_MxN_t H;     MakeIdentityMatrix(H);
	matrix_MxM_t R;     MakeIdentityMatrix(R);      MatScalarMult(R, DEVIATION_MEASUREMENT);
	vector_t     x;
	vector_obs_t z, v;
	vector_t     prev_x;
	double       total_mean_dev = 0.0;  // mean deviation estimated over the whole time period
	double       mean_step = 0.0;       // mean step in space made by the particle

    // Initialize the Kalman filter.
    KalmanFilter<DIM,DIM> kf;
    kf.Init(x0, P0);

    // Iterate the Kalman filter over time following a fancy spiral path.
	prev_x = x0;
	for(int k = 0; k < NUM_TIME_STEPS; ++k) {
		double t = k * (10.0 * M_PI) / (NUM_TIME_STEPS - 1);

		// Generate the next true state. Important: x == x0 at the beginning.
		x[{0}] = t * std::sqrt(std::fabs(t)) * (1 + 0.1*std::cos(5.0*t)) * std::cos(t);
		x[{1}] = t * std::sqrt(std::fabs(t)) * (1 + 0.1*std::cos(5.0*t)) * std::sin(t);
		x[{2}] = t                           * (1 + 0.1*std::cos(5.0*t));
		if (k == 0) {
            ASSERT_LT(NormVecDiff(x, x0), 3 * std::numeric_limits<double>::epsilon());
        } else {
            mean_step += NormVecDiff(x, prev_x);
        }

		// Measurement noise.
		for (int j = 0; j < static_cast<int>(DIM); ++j) {
			v[{j}] = distrib_v(gen);
		}

		// Measurements: z = H*x + v.
		MatVecMult(z, H, x);
		AddVectors(z, z, v);

		// Make a Kalman filter iteration.
        auto model = [&A](vector_t & state, matrix_t & covar) {
            std::unique_ptr<vector_t> old_state(new vector_t());
            *old_state = state;
            MatVecMult(state, A, *old_state);                   // state = A * old_state
            std::unique_ptr<matrix_t> old_covar(new matrix_t());
            MatMult(*old_covar, covar, A);
            MatMult(covar, A, *old_covar);                      // covar = A * old_covar * A^T
        };
		kf.IterateWithModel(model, Q, H, R, z);
		const vector_t & x_est = kf.GetStateVector();
		const matrix_t & P_est = kf.GetCovariance();

		// Mean deviation is a square root of the mean diagonal element of the matrix P_est.
		double mean_dev = 0.0;
		for (int j = 0; j < static_cast<int>(DIM); ++j) { mean_dev += P_est[{j, j}]; }
		mean_dev /= static_cast<double>(DIM);
		total_mean_dev += mean_dev;
		mean_dev = std::sqrt(std::fabs(mean_dev));

		// Print the current time followed the true state vector followed by the noisy
		// measurements followed by the state vector estimated by the Kalman filter
		// followed by the mean eigenvalue of the estimated covariance matrix.
		trackFile << t << SPACE;
		for (int j = 0; j < static_cast<int>(DIM); ++j) { trackFile << x[{j}] << SPACE; }
		for (int j = 0; j < static_cast<int>(DIM); ++j) { trackFile << z[{j}] << SPACE; }
		for (int j = 0; j < static_cast<int>(DIM); ++j) { trackFile << x_est[{j}] << SPACE; }
		trackFile << mean_dev << std::endl;

		prev_x = x;
	}
	trackFile.flush();
	mean_step /= static_cast<double>(NUM_TIME_STEPS - 1);   // first step was missed
	EXPECT_NEAR(mean_step, 0.47272, 1e-5);
	total_mean_dev = std::sqrt(std::fabs(total_mean_dev / static_cast<double>(NUM_TIME_STEPS)));
	EXPECT_NEAR(total_mean_dev, 0.786159, 1e-5);

	summaryFile << "TestKalmanFilter():" << std::endl;
	summaryFile << "model noise deviation: " << DEVIATION_MODEL << std::endl;
	summaryFile << "measurement noise deviation: " << DEVIATION_MEASUREMENT << std::endl;
	summaryFile << "mean step: " << mean_step << std::endl;
	summaryFile << "mean estimated deviation: " << total_mean_dev << std::endl;
    summaryFile << std::endl << std::flush;
}

