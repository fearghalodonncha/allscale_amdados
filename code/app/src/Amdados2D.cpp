//-----------------------------------------------------------------------------
// Author    : Albert Akhriev, albert_akhriev@ie.ibm.com
//             Fearghal O'Donncha, feardonn@ie.ibm.com
// Copyright : IBM Research Ireland, 2017
//-----------------------------------------------------------------------------

#include "allscale/api/user/operator/pfor.h"
#include "allscale/utils/assert.h"
#include "amdados/app/amdados_grid.h"
#include "amdados/app/utils/common.h"
#include "amdados/app/geometry.h"
#include "amdados/app/utils/amdados_utils.h"
#include "amdados/app/utils/matrix.h"
#include "amdados/app/utils/sparse_matrix.h"
#include "amdados/app/utils/configuration.h"
#include "amdados/app/utils/cholesky.h"
#include "amdados/app/utils/lu.h"
#include "gnuplot.h"

#include <cstdlib>
#include <unistd.h>
#include <mutex>
#include <cassert>

using ::allscale::api::user::pfor;
using ::amdados::app::utils::Configuration;

// Components to be included
// 1) Grid structures specific to each subdomain
//    Each grid structure contains information
//    a) structures for three different layers of resolution: (100m (1) ; 20m(2); 4m(3))
//    b) Solution on each layer
//
// 2) Mechanism to switch from layers 1, 2 & 3
// 3) Advection diffusion solver translated from api-prototype
//  ) Boundary synchronizations translated from api
//    Iterative check on error convergence across each subdomain
//    until error norm of all 4 boundaries < threshold
// 4) Data assimilation structures translated from api-prototype
// 5) Matrix operation structures for DA
// 6) Data assimilation solution
// 7) File reads for initial conditions (simple assci format)
// 8) File reads for flowfields and observation data (larger files in structured format)

// Model Initialization & file read
// Create structures here for read from file following variables:
// Ndom, nelems;

// Data structures initialization for grid, advection diffusion and DA

// Advection diffusion solution
// Data assimilation check on observation
// Switch to appropriate grid resolution
// Data assimilation solver
// File output at periodic intervals

namespace amdados {
namespace app {
namespace {

const double TINY = numeric_limits<double>::min() /
           std::pow(numeric_limits<double>::epsilon(),3);

using namespace ::amdados::app::utils;
    //using ::allscale::api::user::pfor;
using ::allscale::api::user::data::Grid;
using ::allscale::api::user::data::GridPoint;

const int NSIDES = 4;                   // number of sides any subdomain has
const int ACTIVE_LAYER = L_100m;        // should be template a parameter eventually

// Structure keeps information about 4-side boundary of a subdomain (Up, Down, Left, Right).
struct Boundary {
    std::vector<double> myself;         // temporary vector for storing this subdomain boundary
    std::vector<double> remote;         // temporary vector for storing remote boundary values
    double              rel_diff;       // relative difference across in-flow borders (Schwarz)
    bool                inflow[NSIDES]; // true, if flow is coming in along a side
    bool                outer[NSIDES];  // true, if a side belongs to the domain's outer boundary
};
typedef Grid<Boundary,2> boundary_grid_t;       // grid of subdomain boundaries

typedef std::pair<double,double>                    flow_t;           // flow components (fx,fy)
typedef Grid<double,3>                              cube_t;           // 3D array of doubles

typedef Matrix<NELEMS_X,NELEMS_Y>                   DA_subfield_t;    // subdomain as matrix
typedef Grid<DA_subfield_t,2>                       field_grid_t;     // grid of subdomains

typedef Vector<SUB_PROBLEM_SIZE>                    DA_vector_t;      // subdomain as vector
typedef Grid<DA_vector_t,2>                         vec_grid_t;       // grid of vectors

typedef Matrix<SUB_PROBLEM_SIZE,SUB_PROBLEM_SIZE>   DA_matrix_t;      // subdomain matrix
typedef Grid<DA_matrix_t,2>                         mat_grid_t;       // grid of matrices

typedef Matrix<NUM_SUBDOMAIN_OBSERVATIONS,
               SUB_PROBLEM_SIZE>                    H_matrix_t; // type of observation matrix
typedef Matrix<NUM_SUBDOMAIN_OBSERVATIONS,
               NUM_SUBDOMAIN_OBSERVATIONS>          R_matrix_t; // observ. noise covarariance

typedef SpMatrix<SUB_PROBLEM_SIZE,SUB_PROBLEM_SIZE> DA_sp_matrix_t;   // subdomain sparse matrix

typedef Grid<Cholesky<SUB_PROBLEM_SIZE>,2>          cholesky_grid_t;  // grid of Cholesky solvers
typedef Grid<LUdecomposition<SUB_PROBLEM_SIZE>,2>   lu_grid_t;        // grid of LU solvers

//#################################################################################################
// @Utilities.
//#################################################################################################

//-------------------------------------------------------------------------------------------------
// Function rounds the value to the nearest integer.
//-------------------------------------------------------------------------------------------------
inline int Round(double val)
{
    assert_true(std::fabs(val) < numeric_limits<int>::max());
    return static_cast<int>(std::floor(val + 0.5));
}

//-------------------------------------------------------------------------------------------------
// Function creates an output directory inside the current one, which is supposed to be the
// project root folder. If the directory is already exist all its content will be deleted.
// TODO: this will not work on Windows, use STL "experimental" instead.
//-------------------------------------------------------------------------------------------------
void CreateAndCleanOutputDir(const Configuration & conf)
{
    const string & dir = conf.asString("output_dir");
    assert_true(!dir.empty());
    string cmd("mkdir -p ");
    cmd += dir;
    int retval = std::system(cmd.c_str());
    retval = std::system("sync");
    retval = std::system((string("/bin/rm -fr ") + dir + "/*.png").c_str());
    retval = std::system((string("/bin/rm -fr ") + dir + "/*.pgm").c_str());
    retval = std::system((string("/bin/rm -fr ") + dir + "/*.jpg").c_str());
    retval = std::system((string("/bin/rm -fr ") + dir + "/*.avi").c_str());
    retval = std::system("sync");
    (void)retval;
}

//-------------------------------------------------------------------------------------------------
// Functions for global reduction across all the subdomains.
// TODO: make them MPI friendly.
//-------------------------------------------------------------------------------------------------
double ReduceMean(const Grid<double,2> & grid)
{
    double sum = 0.0;
    for (int x = 0; x < SubDomGridSize[_X_]; ++x) {
    for (int y = 0; y < SubDomGridSize[_Y_]; ++y) { sum += grid[{x,y}]; }}
    return (sum / static_cast<double>(SubDomGridSize[_X_] * SubDomGridSize[_Y_]));
}
double ReduceAbsMin(const Grid<double,2> & grid)
{
    double v = std::fabs(grid[{0,0}]);
    for (int x = 0; x < SubDomGridSize[_X_]; ++x) {
    for (int y = 0; y < SubDomGridSize[_Y_]; ++y) { v = std::min(v, std::fabs(grid[{x,y}])); }}
    return v;
}
double ReduceAbsMax(const Grid<double,2> & grid)
{
    double v = std::fabs(grid[{0,0}]);
    for (int x = 0; x < SubDomGridSize[_X_]; ++x) {
    for (int y = 0; y < SubDomGridSize[_Y_]; ++y) { v = std::max(v, std::fabs(grid[{x,y}])); }}
    return v;
}

//-------------------------------------------------------------------------------------------------
// Function reads analytic solution from a file. "Analytic solution" here is actually the
// simulation generated by Python code that knows the "true" initial field. Analytic solution
// represents "true" state of the nature, which we never fully observe in reality.
//-------------------------------------------------------------------------------------------------
void ReadAnalyticSolution(const Configuration & conf, cube_t & data, const std::string & filename)
{
    std::chrono::time_point<std::chrono::steady_clock> start = std::chrono::steady_clock::now();
    std::cout << "Reading analytic solution ... " << flush;
    std::ifstream file(filename);
    assert_true(file.good()) << "failed to open the true-solution file: " << filename << endl;
    const int Nt = conf.asInt("Nt");
    int debug_t = 0;                                    // debug time increments on each iteration
    double physical_time = 0.0;
    for (int t = 0; file >> t >> physical_time;) {      // header line contains a time stamps
        assert_true(debug_t == t) << "missed time step: " << debug_t << endl;
        assert_true(t <= Nt) << "too many time steps" << endl;
        ++debug_t;
        for (int x = 0; x < GLOBAL_NELEMS_X; ++x) {     // x,y order is prescribed by Python code
        for (int y = 0; y < GLOBAL_NELEMS_Y; ++y) {
            int i = 0, j = 0;
            double val = 0;
            file >> i >> j >> val;
            if (!((i == x) && (j == y))) {
                assert_true(0) << "mismatch between grid layouts" << endl;
            }
            data[{i,j,t}] = val;
        }}
    }
    assert_true(debug_t == Nt) << "mismatch in number of time steps" << endl;
    std::cout << "done in "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << " seconds" << endl;
}

//-------------------------------------------------------------------------------------------------
// Function gets the observations at certain time and returns them for each subdomain.
// Note, ordinate changes faster than abscissa (row-major stacking in 2D array "field(x,y)").
//-------------------------------------------------------------------------------------------------
void GetObservations(DA_subfield_t & subfield, const point2d_t & idx,
                     const cube_t & analytic_sol, int timestep)
{
    for (int x = 0; x < NELEMS_X; ++x) { int xg = Sub2GloX(idx, x);     // global abscissa
    for (int y = 0; y < NELEMS_Y; ++y) { int yg = Sub2GloY(idx, y);     // global ordinate
        subfield(x,y) = analytic_sol[{xg,yg,timestep}];
    }}
}

//-------------------------------------------------------------------------------------------------
// Function writes the whole property field into a file in binary, grayscaled PGM format,
// where all the values are adjusted to [0..255] interval.
//-------------------------------------------------------------------------------------------------
void WriteImage(const Configuration & conf, const field_grid_t & field,
                const char * title, int time_index,
                std::vector<unsigned char> & image_buffer,
                bool print_image = true, gnuplot::Gnuplot * gp = nullptr)
{
    if (!print_image && (gp == nullptr)) return;
    const bool flipY = false;

    const int ImageSize = GLOBAL_NELEMS_X * GLOBAL_NELEMS_Y;
    if (image_buffer.capacity() < ImageSize) { image_buffer.reserve(ImageSize); }
    image_buffer.resize(ImageSize);

    // Compute the minimum and maximum values of the property field.
    double lower = 0.0, upper = 0.0;
    {
        Grid<double,2> minvalues(SubDomGridSize), maxvalues(SubDomGridSize);
        pfor(Origin, SubDomGridSize, [&](const point2d_t & idx) {
            minvalues[idx] = *(std::min_element(field[idx].begin(), field[idx].end()));
            maxvalues[idx] = *(std::max_element(field[idx].begin(), field[idx].end()));
        });
        lower = ReduceAbsMin(minvalues);
        upper = ReduceAbsMax(maxvalues);
        assert_true(upper - lower > TINY);
    }

    // Convert the property field into one-byte-per-pixel representation.
    pfor(Origin, SubDomGridSize, [&](const point2d_t & idx) {
        const DA_subfield_t & subfield = field[idx];
        for (int y = 0; y < NELEMS_Y; ++y) { int yg = Sub2GloY(idx, y);
        for (int x = 0; x < NELEMS_X; ++x) { int xg = Sub2GloX(idx, x);
            int pos = xg + GLOBAL_NELEMS_X * (flipY ? yg : (GLOBAL_NELEMS_Y - 1 - yg));
            if (!(static_cast<unsigned>(pos) < static_cast<unsigned>(ImageSize))) assert_true(0);
            image_buffer[pos] = static_cast<unsigned char>(
                    Round(255.0 * (subfield(x,y) - lower) / (upper - lower)) );
        }}
    });

    // Write a file in binary, grayscaled PGM format.
    if (print_image) {
        std::stringstream filename;
        filename << conf.asString("output_dir") << "/" << title
                 << std::setfill('0') << std::setw(5) << time_index << ".pgm";
        std::ofstream f(filename.str(),
                std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
        assert_true(f.good()) << "failed to open file for writing: " << filename.str() << endl;
        f << "P5\n" << GLOBAL_NELEMS_X << " " << GLOBAL_NELEMS_Y << "\n" << int(255) << "\n";
        f.write(reinterpret_cast<const char*>(image_buffer.data()), image_buffer.size());
        f << std::flush;
    }

    // Plot the image with Gnuplot, if available.
    if (gp != nullptr) {
        char title[64];
        snprintf(title, sizeof(title)/sizeof(title[0]), "frame%05d", time_index);
        gp->PlotGrayImage(image_buffer.data(), GLOBAL_NELEMS_X, GLOBAL_NELEMS_Y, title, !flipY);
    }
}

//-------------------------------------------------------------------------------------------------
// Function writes into file the image that shows the sensor locations in the whole domain.
//-------------------------------------------------------------------------------------------------
void WriteImageOfSensors(const Configuration & conf,
                         std::vector<unsigned char> & image_buffer, const Grid<H_matrix_t,2> & H)
{
    Grid<DA_subfield_t,2> field(SubDomGridSize);
    pfor(Origin, SubDomGridSize, [&](const point2d_t & idx) {
        DA_subfield_t    & subfield = field[idx];
        const H_matrix_t & subH = H[idx];

        // Everything is set to black except border points painted in dark gray.
        FillMatrix(subfield, 0.0);
        for (int x = 0; x < NELEMS_X; ++x) { subfield(x,0) = subfield(x,NELEMS_Y-1) = 128.0; }
        for (int y = 0; y < NELEMS_Y; ++y) { subfield(0,y) = subfield(NELEMS_X-1,y) = 128.0; }

        // Sensor locations are depicted in white.
        for (int r = 0; r < NUM_SUBDOMAIN_OBSERVATIONS; ++r) {
        for (int c = 0; c < SUB_PROBLEM_SIZE;           ++c) {
            if (subH(r,c) > 0.01) {                 // in fact, H is a matrix of 0/1 values
                div_t res = div(c, NELEMS_Y);       // convert plain index into (x,y) point
                int x = res.quot;
                int y = res.rem;
                bool ok = (DA_subfield_t::sub2ind(x,y) == c);
                assert_true(ok);                                // check index convertion
                subfield(x,y) = 255.0;
            }
        }}
    });
    WriteImage(conf, field, "sensors", 0, image_buffer, true, nullptr);
}

//#################################################################################################
// @Initialization.
//#################################################################################################

//-------------------------------------------------------------------------------------------------
// Function initializes dependent parameters given the primary ones specified by user.
//-------------------------------------------------------------------------------------------------
void InitDependentParams(Configuration & conf)
{
    // Check some global constants.
    assert_true((Origin[_X_] == 0) && (Origin[_Y_] == 0)) << "origin is expected at (0,0)";
    for (Direction dir : { Up, Down, Left, Right }) assert_true((0 <= dir) && (dir < NSIDES));
    assert_true((NELEMS_X >= 3) && (NELEMS_Y >= 3)) << "subdomain must be at least 3x3";

    // Ensure integer values for certain parameters.
    assert_true(conf.asDouble("num_domains_x")      == conf.asInt("num_domains_x"));
    assert_true(conf.asDouble("num_domains_y")      == conf.asInt("num_domains_y"));
    assert_true(conf.asDouble("num_elems_x")        == conf.asInt("num_elems_x"));
    assert_true(conf.asDouble("num_elems_y")        == conf.asInt("num_elems_y"));
    assert_true(conf.asDouble("observation_nx")     == conf.asInt("observation_nx"));
    assert_true(conf.asDouble("observation_ny")     == conf.asInt("observation_ny"));
    assert_true(conf.asDouble("integration_nsteps") == conf.asInt("integration_nsteps"));

    // Check the geometry. In C++ version the whole domain is divided into subdomains.
    assert_true(conf.asInt("num_domains_x") == NUM_DOMAINS_X) << "num_domains_x mismatch" << endl;
    assert_true(conf.asInt("num_domains_y") == NUM_DOMAINS_Y) << "num_domains_y mismatch" << endl;
    assert_true(conf.asInt("num_elems_x") == NELEMS_X) << "num_elems_x mismatch" << endl;
    assert_true(conf.asInt("num_elems_y") == NELEMS_Y) << "num_elems_y mismatch" << endl;

    const int nx = GLOBAL_NELEMS_X;
    const int ny = GLOBAL_NELEMS_Y;

    const double D = conf.asDouble("diffusion_coef");
    assert_true(D > 0.0);

    conf.SetInt("problem_size", nx * ny);
    const double dx = conf.asDouble("domain_size_x") / (nx-1);
    const double dy = conf.asDouble("domain_size_y") / (ny-1);
    assert_true((dx > 0) && (dy > 0));
    conf.SetDouble("dx", dx);
    conf.SetDouble("dy", dy);

    // Deduce the optimal time step from the stability criteria.
    const double TINY = numeric_limits<double>::min() /
               std::pow(numeric_limits<double>::epsilon(),3);
    const double dt_base = conf.asDouble("integration_period") /
                           conf.asDouble("integration_nsteps");
    const double max_vx = conf.asDouble("flow_model_max_vx");
    const double max_vy = conf.asDouble("flow_model_max_vy");
    const double dt = std::min(dt_base,
                        std::min( std::min(dx*dx, dy*dy)/(2.0*D + TINY),
                                  1.0/(std::fabs(max_vx)/dx + std::fabs(max_vy)/dy + TINY) ));
    assert_true(dt > 0);
    conf.SetDouble("dt", dt);
    conf.SetInt("Nt", static_cast<int>(std::ceil(conf.asDouble("integration_period") / dt)));
}

//-------------------------------------------------------------------------------------------------
// Function applies Dirichlet zero boundary condition at the outer border of the domain.
//-------------------------------------------------------------------------------------------------
void ApplyBoundaryCondition(domain_t & state, const point2d_t & idx)
{
    const int Ox = Origin[_X_];  const int Nx = SubDomGridSize[_X_];  const int Sx = NELEMS_X;
    const int Oy = Origin[_Y_];  const int Ny = SubDomGridSize[_Y_];  const int Sy = NELEMS_Y;

    auto & subfield = state[idx].getLayer<ACTIVE_LAYER>();

    if (idx[_X_] == Ox)   { for (int y = 0; y < Sy; ++y) subfield[{   0,y}] = 0.0; } // leftmost
    if (idx[_X_] == Nx-1) { for (int y = 0; y < Sy; ++y) subfield[{Sx-1,y}] = 0.0; } // rightmost

    if (idx[_Y_] == Oy)   { for (int x = 0; x < Sx; ++x) subfield[{x,   0}] = 0.0; } // bottommost
    if (idx[_Y_] == Ny-1) { for (int x = 0; x < Sx; ++x) subfield[{x,Sy-1}] = 0.0; } // topmost
}

//-------------------------------------------------------------------------------------------------
// Function initializes and fills up initial field of density distribution. It is either
// all zeros or a spike at some point and zeros elsewhere. Note, the spike is not very sharp
// to make the field differentiable.
// \param  state       multi-layered structure that keeps density fields of all the subdomains.
// \param  conf        configuration parameters.
// \param  field_type  initial field type is one of: {"zero", "gauss"}.
//-------------------------------------------------------------------------------------------------
void InitialField(domain_t & state, const Configuration & conf, const char * field_type)
{
    if (std::strcmp(field_type, "zero") == 0)
    {
        std::cout << "Initializing field to all zeros" << endl;
        pfor(Origin, SubDomGridSize, [&](const point2d_t & idx) {
            state[idx].setActiveLayer(ACTIVE_LAYER);
            state[idx].forAllActiveNodes([](double & value) { value = 0.0; });
            ApplyBoundaryCondition(state, idx);
        });
    }
    else if (std::strcmp(field_type, "gauss") == 0)
    {
        std::cout << "Initializing field to Gaussian distribution peaked at some point" << endl;

        // Global coordinates of the density spot centre.
        const int cx = Round(conf.asDouble("spot_x") / conf.asDouble("dx"));
        const int cy = Round(conf.asDouble("spot_y") / conf.asDouble("dy"));
        assert_true((0 <= cx) && (cx < GLOBAL_NELEMS_X) &&
                    (0 <= cy) && (cy < GLOBAL_NELEMS_Y))
                    << "high-concentration spot is not inside the domain" << endl;

        // Parameters of the global 2D Gaussian model of the spike.
        const double sigma = 1.0;                           // in logical units (point indices)
        const double a = conf.asDouble("spot_density") / (std::pow(sigma,2) * 2.0 * M_PI);
        const double b = 1.0 / (2.0 * std::pow(sigma,2));

        // For all the subdomains initialize the spike distribution by parts.
        pfor(Origin, SubDomGridSize, [&](const point2d_t & idx) {
            // Set active layer to zero at the beginning.
            state[idx].setActiveLayer(ACTIVE_LAYER);
            state[idx].forAllActiveNodes([](double & value) { value = 0.0; });

            auto & subfield = state[idx].getLayer<ACTIVE_LAYER>();
            for (int x = 0; x < NELEMS_X; ++x) {
            for (int y = 0; y < NELEMS_Y; ++y) {
                int dx = Sub2GloX(idx,x) - cx;
                int dy = Sub2GloY(idx,y) - cy;
                if ((std::abs(dx) <= 4*sigma) && (std::abs(dy) <= 4*sigma)) {
                    subfield[{x,y}] += a * std::exp(-b * (dx*dx + dy*dy));
                }
            }}
            ApplyBoundaryCondition(state, idx);
        });
    }
    else
    {
        assert_true(0) << "InitialField(): unknown field type" << endl;
    }
}

//#################################################################################################
// @Kalman filter stuff.
//#################################################################################################

//-------------------------------------------------------------------------------------------------
// Function computes the initial covariance matrix as function of exponential distance.
//-------------------------------------------------------------------------------------------------
void InitialCovar(const Configuration & conf, DA_matrix_t & P)
{
    using SF = Matrix<NELEMS_X,NELEMS_Y>;   // sub-field type

    // Here we express correlation distances in logical coordinates of nodal points.
    const double variance = conf.asDouble("model_ini_var");
    const double covar_radius = conf.asDouble("model_ini_covar_radius");
    const double sx = std::max(NELEMS_X * covar_radius / conf.asDouble("domain_size_x"), 1.0);
    const double sy = std::max(NELEMS_Y * covar_radius / conf.asDouble("domain_size_y"), 1.0);
    const int Rx = Round(std::ceil(3.0 * sx));
    const int Ry = Round(std::ceil(3.0 * sx));

    FillMatrix(P, 0.0);
    for (int u = 0; u < NELEMS_X; ++u) {
    for (int v = 0; v < NELEMS_Y; ++v) {
        int i = SF::sub2ind(u,v);
        for (int x = u-Rx; x <= u+Rx; ++x) { if ((0 <= x) && (x < NELEMS_X)) { double dx = (u-x)/sx;
        for (int y = v-Ry; y <= v+Ry; ++y) { if ((0 <= y) && (y < NELEMS_Y)) { double dy = (v-y)/sy;
            int j = SF::sub2ind(x,y);
            if (i <= j) P(i,j) = P(j,i) = variance * std::exp(-0.5 * (dx*dx + dy*dy));
        }}}}
    }}
}

//-------------------------------------------------------------------------------------------------
// Function computes the model noise covariance matrix.
//-------------------------------------------------------------------------------------------------
void ComputeQ(const Configuration & conf, DA_matrix_t & Q)
{
    std::uniform_real_distribution<double> distrib;
    std::mt19937                           gen(std::time(nullptr));
    const double                           model_noise_Q = conf.asDouble("model_noise_Q");

    MakeIdentityMatrix(Q);
    for (int k = 0; k < SUB_PROBLEM_SIZE; ++k) {
        Q(k,k) += model_noise_Q * distrib(gen);
    }
}

//-------------------------------------------------------------------------------------------------
// Function computes the measurement noise covariance matrix.
//-------------------------------------------------------------------------------------------------
void ComputeR(const Configuration & conf, R_matrix_t & R)
{
    std::uniform_real_distribution<double> distrib;
    std::mt19937                           gen(std::time(nullptr));
    const double                           model_noise_R = conf.asDouble("model_noise_R");

    MakeIdentityMatrix(R);
    for (int k = 0; k < NUM_SUBDOMAIN_OBSERVATIONS; ++k) {
        R(k,k) += model_noise_R * distrib(gen);
    }
}

//-------------------------------------------------------------------------------------------------
// Function initializes observation matrix H. Function evenly distributes sensors in the domain.
// It uses gradient descent to minimize objective function, which measures the mutual sensor
// points' expulsion and their expulsion from the borders. Experiments showed that quadratic
// distances (used here) work better than absolute values of the distances. See the Memo named
// "Sensor Placement" for details.
//-------------------------------------------------------------------------------------------------
void ComputeH(const Configuration & conf, H_matrix_t & H, const point2d_t & idx)
{
    const int N = NUM_SUBDOMAIN_OBSERVATIONS;               // short-hand alias
    const int NN = N * N;

    // Function evaluates objective function and its gradient.
    auto Evaluate = [](double & J, std::vector<double> & gradJ,
                             const std::vector<double> & x,
                             const std::vector<double> & y)
    {
        const double EPS = std::sqrt(numeric_limits<double>::epsilon());

        J = 0.0;
        gradJ.resize(2*N);
        std::fill(gradJ.begin(), gradJ.end(), 0.0);

        for (int i = 0; i < N; ++i) {
            // Reciprocal distances to subdomain borders.
            const double r_x1 = 1.0 / (std::pow(      x[i],2) + EPS);
            const double r_x2 = 1.0 / (std::pow(1.0 - x[i],2) + EPS);
            const double r_y1 = 1.0 / (std::pow(      y[i],2) + EPS);
            const double r_y2 = 1.0 / (std::pow(1.0 - y[i],2) + EPS);

            J += (r_x1 + r_x2 +
                  r_y1 + r_y2);

            double gx = 0.0, gy = 0.0;
            for (int j = 0; j < N; ++j) {
                double dx = x[i] - x[j];
                double dy = y[i] - y[j];
                double sqdist = dx*dx + dy*dy + EPS;
                J  += 1.0 / sqdist;
                gx -= dx / std::pow(sqdist,2);
                gy -= dy / std::pow(sqdist,2);
            }
            gradJ[i  ] = 2.0 * (gx - x[i] * std::pow(r_x1,2) + (1.0 - x[i]) * std::pow(r_x2,2));
            gradJ[i+N] = 2.0 * (gy - y[i] * std::pow(r_y1,2) + (1.0 - y[i]) * std::pow(r_y2,2));
        }

        J /= NN;
        std::transform(gradJ.begin(), gradJ.end(), gradJ.begin(), [=](double x){ return x/NN; });
    };

    std::vector<double> x(N), y(N);         // sensors' abscissas and ordinates

    // Generates initial space distribution of sensor points.
    {
        std::mt19937 gen(std::time(nullptr) + idx[_X_] * NUM_DOMAINS_Y + idx[_Y_]);
        std::uniform_real_distribution<double> distrib(0.0, 1.0);
        std::generate(x.begin(), x.end(), [&](){ return distrib(gen); });
        std::generate(y.begin(), y.end(), [&](){ return distrib(gen); });
    }

    // Minimize the objective function by gradient descent.
    {
        const double TINY = numeric_limits<double>::min() /
                   std::pow(numeric_limits<double>::epsilon(),3);

        const double DOWNSCALE = 0.1;
        const double INITIAL_STEP = 0.1;
        const double TOL = numeric_limits<double>::epsilon() * std::log(N);

        std::vector<double> x_new(N), y_new(N);             // sensors' abscissas and ordinates
        std::vector<double> gradJ(2*N), gradJ_new(2*N);     // gradients of objective function
        double              J = 0.0, J_new = 0.0;           // value of objective function
        double              step = INITIAL_STEP;            // step in gradient descent

        Evaluate(J, gradJ, x, y);
        for (bool proceed = true; proceed && (step > TINY);) {
            bool is_inside = true;
            for (int k = 0; (k < N) && is_inside; ++k) {
                x_new[k] = x[k] - step * gradJ[k  ];
                y_new[k] = y[k] - step * gradJ[k+N];
                is_inside = ((0.0 <= x_new[k]) && (x_new[k] <= 1.0) &&
                             (0.0 <= y_new[k]) && (y_new[k] <= 1.0));
            }
            if (!is_inside) {
                step *= DOWNSCALE;
                continue;
            }
            Evaluate(J_new, gradJ_new, x_new, y_new);
            if (J < J_new) {
                step *= DOWNSCALE;
                continue;
            }
            proceed = (J - J_new > J * TOL);
            x = x_new;
            y = y_new;
            J = J_new;
            gradJ = gradJ_new;
            step *= 2.0;
        }
    }

    // Now we have evenly distributed sensor locations, which will be used to initialize matrix H.
    FillMatrix(H, 0.0);
    for (int k = 0; k < N; ++k) {
        int xk = std::min(std::max(static_cast<int>(std::floor(x[k] * NELEMS_X)), 0), NELEMS_X-1);
        int yk = std::min(std::max(static_cast<int>(std::floor(y[k] * NELEMS_Y)), 0), NELEMS_Y-1);
        int point_index = DA_subfield_t::sub2ind(xk, yk);
        H(k,point_index) = 1.0;
    }
}

//#################################################################################################
// @Advection-diffusion PDE stuff.
//#################################################################################################

//-------------------------------------------------------------------------------------------------
// Function computes flow components given a time.
// \return a pair of flow components (flow_x, flow_y).
//-------------------------------------------------------------------------------------------------
flow_t Flow(const Configuration & conf, const double physical_time)
{
    double max_vx = conf.asDouble("flow_model_max_vx");
    double max_vy = conf.asDouble("flow_model_max_vy");
    return flow_t(
        -max_vx * std::sin(0.1 * physical_time / conf.asDouble("integration_period") - M_PI),
        -max_vy * std::sin(0.2 * physical_time / conf.asDouble("integration_period") - M_PI) );
}

//-------------------------------------------------------------------------------------------------
// Function initializes inverse matrix of implicit Euler time-integrator:
// B * x_{t+1} = x_{t}, where B = A^{-1} is the matrix returned by this function.
// The matrix must be inverted while iterating forward in time: x_{t+1} = A * x_{t}.
// Note, the matrix we generate here is acting on a subdomain.
// Note, the model matrix B is supposed to be a sparse one. For now, since we do not have
// a fast utility for sparse matrix inversion, we define B as a dense one with many zeros.
//-------------------------------------------------------------------------------------------------
void InverseModelMatrix(DA_matrix_t & B, const Configuration & conf,
                        const Boundary & boundary, const flow_t flow)
{
    using SF = Matrix<NELEMS_X,NELEMS_Y>;   // sub-field type

    const int Nx = NELEMS_X;    // short-hand aliases
    const int Ny = NELEMS_Y;

    const double D  = conf.asDouble("diffusion_coef");
    const double dx = conf.asDouble("dx");
    const double dy = conf.asDouble("dy");
    const double dt = conf.asDouble("dt");

    const double rho_x = D * dt / std::pow(dx,2);
    const double rho_y = D * dt / std::pow(dy,2);

    const double v0x = 2.0 * dx / dt;
    const double v0y = 2.0 * dy / dt;

    const double vx = flow.first  / v0x;
    const double vy = flow.second / v0y;

    // This operation can be avoided in case of sparse matrix.
    FillMatrix(B, 0.0);

    // Process the internal and boundary subdomain points separately.
    // At each border we hit outside points with finite difference scheme.
    // We choose replacing values inside subdomain in such way that if there is
    // no incoming flow, the field derivative along the border normal is zero.
    for (int x = 0; x < Nx; ++x) {
    for (int y = 0; y < Ny; ++y) {
        int i = SF::sub2ind(x,y);

        if ((x == 0) || (x+1 == Nx) || (y == 0) || (y+1 == Ny)) {
            B(i,i) += 1 + 2*(rho_x + rho_y);

            if (x == 0) {
                if (boundary.inflow[Left]) {
                    B(i,SF::sub2ind(x  ,y)) += - 2*vx - rho_x;
                    B(i,SF::sub2ind(x+1,y)) += + 2*vx - rho_x;
                } else {
                    B(i,SF::sub2ind(x+1,y)) += - vx - rho_x;
                    B(i,SF::sub2ind(x+1,y)) += + vx - rho_x;
                }
            } else if (x == Nx-1) {
                if (boundary.inflow[Right]) {
                    B(i,SF::sub2ind(x-1,y)) += - 2*vx - rho_x;
                    B(i,SF::sub2ind(x  ,y)) += + 2*vx - rho_x;
                } else {
                    B(i,SF::sub2ind(x-1,y)) += - vx - rho_x;
                    B(i,SF::sub2ind(x-1,y)) += + vx - rho_x;
                }
            } else {
                B(i,SF::sub2ind(x-1,y)) += - vx - rho_x;
                B(i,SF::sub2ind(x+1,y)) += + vx - rho_x;
            }

            if (y == 0) {
                if (boundary.inflow[Down]) {
                    B(i,SF::sub2ind(x,y  )) += - 2*vy - rho_y;
                    B(i,SF::sub2ind(x,y+1)) += + 2*vy - rho_y;
                } else {
                    B(i,SF::sub2ind(x,y+1)) += - vy - rho_y;
                    B(i,SF::sub2ind(x,y+1)) += + vy - rho_y;
                }
            } else if (y == Ny-1) {
                if (boundary.inflow[Up]) {
                    B(i,SF::sub2ind(x,y-1)) += - 2*vy - rho_y;
                    B(i,SF::sub2ind(x,y  )) += + 2*vy - rho_y;
                } else {
                    B(i,SF::sub2ind(x,y-1)) += - vy - rho_y;
                    B(i,SF::sub2ind(x,y-1)) += + vy - rho_y;
                }
            } else {
                B(i,SF::sub2ind(x,y-1)) += - vy - rho_y;
                B(i,SF::sub2ind(x,y+1)) += + vy - rho_y;
            }
        } else {
            B(i,i) = 1 + 2*(rho_x + rho_y);
            B(i,SF::sub2ind(x-1,y)) = - vx - rho_x;
            B(i,SF::sub2ind(x+1,y)) = + vx - rho_x;
            B(i,SF::sub2ind(x,y-1)) = - vy - rho_y;
            B(i,SF::sub2ind(x,y+1)) = + vy - rho_y;
        }
    }}
}

//-------------------------------------------------------------------------------------------------
// Function implements the idea of Schwartz method where the boundary values of subdomain
// are get updated depending on flow direction.
//-------------------------------------------------------------------------------------------------
double SchwarzUpdate(const Configuration & conf,
                      Boundary & border, const point2d_t & idx,
                      domain_t & domain, flow_t flow)
{
    // Origin and the number of subdomains in both directions.
    const int Ox = Origin[_X_];  const int Nx = SubDomGridSize[_X_];
    const int Oy = Origin[_Y_];  const int Ny = SubDomGridSize[_Y_];

    // Index increments and corresponding directions of a neighbour (remote) subdomain.
    Direction remote_dir[NSIDES];
    point2d_t remote_idx[NSIDES];

    remote_dir[Left ] = Right;  remote_idx[Left ] = point2d_t{-1,0};
    remote_dir[Right] = Left;   remote_idx[Right] = point2d_t{+1,0};
    remote_dir[Down ] = Up;     remote_idx[Down ] = point2d_t{0,-1};
    remote_dir[Up   ] = Down;   remote_idx[Up   ] = point2d_t{0,+1};

    // Function updates a boundary depending on flow direction and accumulates the difference
    // between this and neighbour (remote) subdomain according to Schwarz method.
    auto UpdBoundary = [&](Direction dir, bool is_outer, const point2d_t & idx,
                           double & numer_sum, double & denom_sum)
    {
        border.outer[dir] = is_outer;
        border.inflow[dir] = false;
        if (is_outer) return;                       // nothing to do with outer border

        // Make a normal vector pointing outside the subdomain.
        int normal_x = (dir == Right) ? +1 : ((dir == Left) ? -1 : 0);
        int normal_y = (dir ==    Up) ? +1 : ((dir == Down) ? -1 : 0);

        // Update the boundary points if flow enters the subdomain.
        if (normal_x * flow.first + normal_y * flow.second < 0) {
            border.inflow[dir] = true;
            border.myself = domain[idx].getBoundary(dir);
            border.remote = domain[idx + remote_idx[dir]].getBoundary(remote_dir[dir]);
            domain[idx].setBoundary(dir, border.remote);

            // Compute and accumulate aggregated difference between subdomains' borders.
            double rsum = 0.0, msum = 0.0, diff = 0.0;
            assert_true(border.myself.size() == border.remote.size());
            for (auto r = border.remote.begin(),
                      m = border.myself.begin(); m != border.myself.end(); ++m, ++r) {
                diff += std::fabs(*r - *m);
                rsum += std::fabs(*r);
                msum += std::fabs(*m);
            }
            numer_sum += diff;
            denom_sum += std::max(rsum, msum);
        }
    };

    // Update subdomain boundaries.
    double numer_sum = 0.0, denom_sum = 0.0;

    UpdBoundary(Left,  (idx[_X_] == Ox),   idx, numer_sum, denom_sum);
    UpdBoundary(Right, (idx[_X_] == Nx-1), idx, numer_sum, denom_sum);
    UpdBoundary(Down,  (idx[_Y_] == Oy),   idx, numer_sum, denom_sum);
    UpdBoundary(Up,    (idx[_Y_] == Ny-1), idx, numer_sum, denom_sum);

    border.rel_diff = numer_sum / std::max(denom_sum, TINY);
    return border.rel_diff;
}

//-------------------------------------------------------------------------------------------------
// Using model matrix A, the function integrates advection-diffusion equation forward in time
// (x_{t+1} = A * x_{t}) and records all the solutions - state fields. These fields are
// considered as the "true" state of the nature and the source of the "true" observations.
//-------------------------------------------------------------------------------------------------
void ComputeTrueFields(const Configuration & conf)
{
/*
    std::cout << "Computing the observations, a.k.a 'true' density fields" << endl << flush;
    //true_fields = np.zeros((conf.nx, conf.ny, conf.Nt))

    //std::unique_ptr<gnuplot::Gnuplot> gp(new gnuplot::Gnuplot(nullptr, "-background yellow"));
    std::unique_ptr<gnuplot::Gnuplot> gp(new gnuplot::Gnuplot());   // mac os

    // Global grid structures defined for all the subdomains.
    // The difference between subdomains 'curr[i]' and 'state[i]' is that the latter
    // can handle multi-resolution case and communicate with the neighbour subdomains. We copy
    // 'state[i]' to 'curr[i]' on each iteration, do processing and copy it back.

    mat_grid_t                 B(SubDomGridSize);           // inverse model matrices
    field_grid_t               curr(SubDomGridSize);        // copy of the current subdomain states
    field_grid_t               next(SubDomGridSize);        // copy of the next subdomain states
    lu_grid_t                  lu(SubDomGridSize);          // objects for LU decomposition
    std::vector<unsigned char> image_buffer;                // temporary buffer
    domain_t                   state(SubDomGridSize);       // states of the density fields
    boundary_grid_t            boundary(SubDomGridSize);    // neighboring boundary vaklues

    // Generate the initial density field.
    InitialField(state, curr, conf);

    // Time integration forward in time.
    for (int t = 0, Nt = conf.asInt("Nt"); t < Nt; ++t) {
        //std::cout << '+' << flush;
        WriteImage(conf, curr, "field", t, image_buffer, true, gp.get());

        const double physical_time = t * conf.asDouble("dt");
        do {
WriteImage(conf, curr, "field", t, image_buffer, false, gp.get());
            //std::cout << '.' << flush;
            pfor(Origin, SubDomGridSize, [&](const point2d_t & idx) {
                MatrixFromAllscale(curr[idx], state[idx].getLayer<L_100m>());

                InverseModelMatrix(B[idx], conf, physical_time);    // compute inverse model matrix
                lu[idx].Init(B[idx]);                               // decompose: B = L * U
                lu[idx].Solve(next[idx], curr[idx]);                // x_{t+1} = B^{-1} * x_{t}
                curr[idx] = next[idx];

                AllscaleFromMatrix(state[idx].getLayer<L_100m>(), next[idx]);
            });
        } while (SchwarzUpdate(conf, boundary,
                                state, physical_time) > conf.asDouble("schwartz_tol"));
    }
    std::cout << endl << endl << endl;
*/
}

//-------------------------------------------------------------------------------------------------
// Using model matrix A, the function integrates advection-diffusion equation forward in time
// (x_{t+1} = A * x_{t}) and records all the solutions - state fields. These fields are
// considered as the "true" state of the nature and the source of the "true" observations.
//-------------------------------------------------------------------------------------------------
void RunDataAssimilation(const Configuration & conf, const cube_t & analytic_sol)
{
    std::cout << "Running simulation with data assimilation" << endl << flush;

    std::unique_ptr<gnuplot::Gnuplot> gp(new gnuplot::Gnuplot());
    std::vector<unsigned char>        image_buffer;                // temporary global image buffer

    domain_t        state(SubDomGridSize);
    field_grid_t    observations(SubDomGridSize);
    boundary_grid_t boundaries(SubDomGridSize);

    mat_grid_t      B(SubDomGridSize);              // inverse model matrices
    field_grid_t    curr(SubDomGridSize);           // copy of the current subdomain states
    field_grid_t    next(SubDomGridSize);           // copy of the next subdomain states
    lu_grid_t       lu(SubDomGridSize);             // objects for LU decomposition

    Grid<H_matrix_t,2>  H(SubDomGridSize);  // observation matrices of all the subdomains
    Grid<R_matrix_t,2>  R(SubDomGridSize);  // observation noise covariances of all the subdomains
    Grid<DA_matrix_t,2> Q(SubDomGridSize);  // process noise covariances of all the subdomains

    //InitialField(state, conf, "zero");
    InitialField(state, conf, "gauss");

    // Observation matrix is initializes once at the beginning.
    pfor(Origin, SubDomGridSize, [&](const point2d_t & idx) { ComputeH(conf, H[idx], idx); });
    WriteImageOfSensors(conf, image_buffer, H);     // visualization

    // Time integration forward in time.
    for (int t = 0, Nt = conf.asInt("Nt"); t < Nt; ++t) {
        std::cout << '.' << flush;

        const double physical_time = t * conf.asDouble("dt");
        const flow_t flow = Flow(conf, physical_time);

        pfor(Origin, SubDomGridSize, [&](const point2d_t & idx) {
            SchwarzUpdate(conf, boundaries[idx], idx, state, flow);
            ApplyBoundaryCondition(state, idx);

            MatrixFromAllscale(curr[idx], state[idx].getLayer<ACTIVE_LAYER>());

            InverseModelMatrix(B[idx], conf, boundaries[idx], flow);  // get inverse model matrix
            lu[idx].Init(B[idx]);                                     // decompose: B = L * U
            lu[idx].Solve(next[idx], curr[idx]);                      // x_{t+1} = B^{-1} * x_{t}
            curr[idx] = next[idx];                                    // update state

            AllscaleFromMatrix(state[idx].getLayer<ACTIVE_LAYER>(), next[idx]);
            ApplyBoundaryCondition(state, idx);

            // Covariance matrices are allowed to change over time.
            ComputeQ(conf, Q[idx]);
            ComputeR(conf, R[idx]);
        });
        WriteImage(conf, curr, "field", t, image_buffer, true, gp.get());   // visualization

        //GetObservations(curr[idx], idx, analytic_sol, t);

        //pfor(Origin, SubDomGridSize, [&](const point2d_t & idx) {
            //MatrixFromAllscale(curr[idx], state[idx].getLayer<L_100m>());

            //InverseModelMatrix(B[idx], conf, physical_time);    // compute inverse model matrix
            //lu[idx].Init(B[idx]);                               // decompose: B = L * U
            //lu[idx].Solve(next[idx], curr[idx]);                // x_{t+1} = B^{-1} * x_{t}
            //curr[idx] = next[idx];

            //AllscaleFromMatrix(state[idx].getLayer<L_100m>(), next[idx]);
        //});


        //GetObservations(observations, analytic_sol, t);
        //SchwarzUpdate(conf, boundaries, state, physical_time);

        //WriteImage(conf, observations, "field", t, image_buffer, false, gp.get());
    }
    std::cout << endl << endl << endl;
}


#if 0
{
            // 1) update boundaries
            for (Direction dir : { Up, Down, Left, Right }) {

                // skip global boarder
                if (dir == Up    && idx[0] == 0)         continue; // if direction == up and no neighbour to south
                if (dir == Down  && idx[0] == size[0]-1) continue;
                if (dir == Left  && idx[1] == 0)         continue;
                if (dir == Right && idx[1] == size[1]-1) continue;

                // obtain the local boundary
                auto local_boundary = cur.getBoundary(dir);

                // obtain the neighboring boundary
                auto remote_boundary =
                    (dir == Up)   ? A[idx + point2d_t{-1,0}].getBoundary(Down)  : // remote boundary is bottom strip of neighbour
                    (dir == Down) ? A[idx + point2d_t{ 1,0}].getBoundary(Up)    : // remote boundary is top of neighbour
                    (dir == Left) ? A[idx + point2d_t{0,-1}].getBoundary(Right) : // remote boundary is left of domain
                                    A[idx + point2d_t{0, 1}].getBoundary(Left);

                // compute local flow in domain to decide if flow in or out of domain
                if (dir == Down) ny = -1; // flow into domain from below
                if (dir == Left) nx = -1; // flow into domain from left
                double flow_boundary =  nx*flowu + ny*flowv;
                // TODO: scale the boundary vectors to the same resolution

                // compute updated boundary
                assert(local_boundary.size() == remote_boundary.size());
                if (flow_boundary < 0) {  // then flow into domain need to update boundary with neighbour value
                    for(size_t i = 0; i<local_boundary.size(); i++) {
                        // for now, we just take the average
                        // need to update this to account for flow direction (Fearghal)
                        local_boundary[i] = remote_boundary[i];
                    }
                }

//std::cout << "DIRECTION: "
//<< (dir == Up ? "Up" : (dir == Down ? "Down" : (dir == Left ? "Left" : "Right"))) << std::endl;
assert(CheckNoNan(res.getLayer<L_100m>()));

                // update boundary in result
                res.setBoundary(dir,local_boundary);

assert(CheckNoNan(res.getLayer<L_100m>()));
            }

}
#endif

} // anonymous namespace
} // namespace app
} // namespace allscale

int Amdados2DMain()
{
    std::cout << "***** Amdados2D application *****" << std::endl << std::endl << std::flush;
    using namespace ::amdados::app;
    using namespace ::amdados::app::utils;

    // Computing the observations, a.k.a 'true' density fields
   // ComputeTrueFields(conf);

    try {
        // Read application parameters from configuration file, prepare the output directory.
        Configuration conf;
        conf.ReadConfigFile("amdados.conf");
        InitDependentParams(conf);
        conf.PrintParameters();
        CreateAndCleanOutputDir(conf);

        // Read analytic solution previously computed by the Python application "Amdados2D.py".
        std::unique_ptr<cube_t> analytic_sol;
        analytic_sol.reset(new cube_t(size3d_t{GLOBAL_NELEMS_X,GLOBAL_NELEMS_Y,conf.asInt("Nt")}));
        ReadAnalyticSolution(conf, *analytic_sol,
                    conf.asString("output_dir") + "/" + conf.asString("analytic_solution"));

        RunDataAssimilation(conf, *analytic_sol);
    } catch (std::exception & e) {
        std::cout << endl << "exception: " << e.what() << endl << endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cout << endl << "Unsupported exception" << endl << endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

//Compute(zero, size_global);
//std::cout << "active layer: " << A[{1,1}].getLayer<L_100m>() << std::endl;  // XXX ???



//long max_x = -1, max_y = -1;

        //// Get the remote neighboring boundaries for each subdomain, skipping the global border.
        //pfor(Origin, SubDomGridSize, [&](const point2d_t & idx) {
            //for (Direction dir : { Up, Down, Left, Right }) {
                //assert_true((0 <= dir) && (dir < 4));
                //if ((dir == Up)    && (idx[0] == Origin[0]))           continue;
                //if ((dir == Down)  && (idx[0] == SubDomGridSize[0]-1)) continue;
                //if ((dir == Left)  && (idx[1] == Origin[1]))           continue;
                //if ((dir == Right) && (idx[1] == SubDomGridSize[1]-1)) continue;
                //glo_boundary[idx].side[dir] =
                    //(dir == Up)   ? glo_state[idx + point2d_t{-1,0}].getBoundary(Down)  :
                    //(dir == Down) ? glo_state[idx + point2d_t{ 1,0}].getBoundary(Up)    :
                    //(dir == Left) ? glo_state[idx + point2d_t{0,-1}].getBoundary(Right) :
                                    //glo_state[idx + point2d_t{0, 1}].getBoundary(Left);
            //}
//g_mutex.lock();
//max_x = std::max(max_x, idx[0]);
//max_y = std::max(max_y, idx[1]);
//g_mutex.unlock();
        //});

        ////auto & kkk = glo_state[{0,0}];
//auto & hhh = glo_boundary[{3,4}];
//for (Direction dir : { Up, Down, Left, Right }) {
    //std::cout << "dir = " << dir << endl;
    //std::cout << "size: " << hhh.side[dir].size() << endl << endl;
//}
//std::cout << "max_x: " << max_x << ", max_y: " << max_y << endl;
//auto & lll = glo_state[{-100,0}]; std::cout << &lll << endl;
//return;

