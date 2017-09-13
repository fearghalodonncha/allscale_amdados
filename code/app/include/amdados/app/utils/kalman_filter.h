//-----------------------------------------------------------------------------
// Author    : Albert Akhriev, albert_akhriev@ie.ibm.com
// Copyright : IBM Research Ireland, 2017
//-----------------------------------------------------------------------------

#pragma once

namespace amdados {
namespace app {
namespace utils {

//=================================================================================================
// Class implements a Kalman filter using AllScale API grid for matrices and vectors.
//=================================================================================================
template<int PROBLEM_SIZE, int NUM_OBSERVATIONS>
class KalmanFilter
{
public:
    typedef Matrix<PROBLEM_SIZE, PROBLEM_SIZE>         matrix_t;
    typedef Matrix<NUM_OBSERVATIONS, PROBLEM_SIZE>     matrix_OxN_t;
    typedef Matrix<PROBLEM_SIZE, NUM_OBSERVATIONS>     matrix_NxO_t;
    typedef Matrix<NUM_OBSERVATIONS, NUM_OBSERVATIONS> matrix_OxO_t;
    typedef Vector<PROBLEM_SIZE>                       vector_t;
    typedef Vector<NUM_OBSERVATIONS>                   vector_obs_t;

private:
    Cholesky<NUM_OBSERVATIONS>    m_chol;  // Cholesky decomposition solver
    LUdecomposition<PROBLEM_SIZE> m_lu;    // LU decomposition solver

    vector_t     m_x_prior;     // placeholder for the vector x_{k|k-1} = A * x
    vector_t     m_x_tmp;       // placeholder for the vector x_{k|k-1} = A * x
    vector_obs_t m_y;           // placeholder vector of observations
    vector_obs_t m_invSy;       // placeholder vector for S^{-1} * y
    matrix_OxO_t m_S;           // placeholder for the matrix S = H * P_{k|k-1} * H^t + R
    matrix_t     m_P_prior;     // placeholder for the matrix P_{k|k-1}
    matrix_t     m_P_tmp;       // placeholder for the matrix P_{k|k-1}
    matrix_NxO_t m_PHt;         // placeholder for the matrix P_{k|k-1} * H^t
    matrix_OxN_t m_HP;          // placeholder for the matrix H * P_{k|k-1}
    matrix_OxN_t m_invSHP;      // placeholder for the matrix S^{-1} * H * P_{k|k-1}

public:
//-------------------------------------------------------------------------------------------------
// Constructor (by default all vector/matrix variables are initialized by zeros).
//-------------------------------------------------------------------------------------------------
KalmanFilter()
{
}

//-------------------------------------------------------------------------------------------------
// Function makes an iteration of Kalman filter which includes prediction and correction phases.
// It is assumed that the model adapter can compute the prior estimations of state and covariance,
// e.g. for the linear transition operator A: x_prior = A * x, P_prior = A * P * A^t.
// \param  A  model matrix for computing the prior estimations of state and covariance.
// \param  Q  process noise (w_k) covariance.
// \param  H  observation model: z_k = H_k * x_k + v_k.
// \param  R  measurement noise (v_k) covariance.
// \param  z  vector of observations.
// \param  x  in: current state; out: new state.
// \param  P  in: current covariance; out: new covariance.
//-------------------------------------------------------------------------------------------------
template<typename MODEL_MATRIX>
void Iterate(MODEL_MATRIX & A, const matrix_t & Q,
             const matrix_OxN_t & H, const matrix_OxO_t & R,
             const vector_obs_t & z,
             vector_t & x, matrix_t & P)
{
    // Model updates the state and covariance:
    // x_prior = A * x, P_prior = A * P * A^t, where A is some linear operator (matrix here).
    m_x_prior = x;
    m_P_prior = P;
    UpdateState(A, m_x_prior, m_P_prior);
    // P_prior += Q
    AddMatrices(m_P_prior, m_P_prior, Q);
    // Estimate posterior state and covariance.
    PosteriorEstimation(H, R, z, x, P);
}

//-------------------------------------------------------------------------------------------------
// Function makes an iteration of discrete-time Kalman filter which includes prediction and
// correction phases in the case when only inverse model matrix is available. It is assumed that
// the process model has the form: x_{t+1} = A * x_{t} + w_t, and the prior estimations of state
// and covariance reads: x_prior = A * x, P_prior = A * P * A^t.
// \param  x  in: current state; out: new state.
// \param  P  in: current covariance; out: new covariance.
// \param  B  inverse model matrix: B = A^{-1}.
// \param  Q  process noise (w_t) covariance.
// \param  H  observation model: z_t = H_t * x_t + v_t.
// \param  R  measurement noise (v_t) covariance.
// \param  z  vector of observations.
//-------------------------------------------------------------------------------------------------
void IterateInverse(
          VectorView<PROBLEM_SIZE>                   & x,
          Matrix<PROBLEM_SIZE, PROBLEM_SIZE>         & P,
    const Matrix<PROBLEM_SIZE, PROBLEM_SIZE>         & B,
    const Matrix<PROBLEM_SIZE, PROBLEM_SIZE>         & Q,
    const Matrix<NUM_OBSERVATIONS, PROBLEM_SIZE>     & H,
    const Matrix<NUM_OBSERVATIONS, NUM_OBSERVATIONS> & R,
    const VectorView<NUM_OBSERVATIONS>               & z)
{
    // Propagate state and covariance one timestep ahead and obtain prior estimations:
    // x_prior = A * x, P_prior = A * P * A^t, where A is avaliable via its inversion B = A^{-1}.
    m_lu.Init(B);                           // decompose: B = L * U
    m_lu.Solve(m_x_prior, x);               // x_prior = B^{-1} * x_{t}
#if 1
    m_lu.BatchSolve(m_P_prior, P);          // P_prior = B^{-1} * P  (P symmetric!)
    P = m_P_prior;                          // use P as a temporary matrix
    m_lu.BatchSolveTr(m_P_prior, P);        // P_prior = B^{-1} * (B^{-1} * P)^t = A * P * A^t
    AddMatrices(m_P_prior, m_P_prior, Q);   // P_prior = A * P * A^t + Q
    Symmetrize(m_P_prior);                  // correct loss of symmetry due to round-off errors

    // Estimate posterior state and covariance.
    PosteriorEstimation(H, R, z, x, P);
#else
    #pragma message("!!!!!!! B E W A R E: TEMPORARY CODE !!!!!!! NO KALMAN FILTERING")
    x = m_x_prior;
#endif
}

//-------------------------------------------------------------------------------------------------
// Function propagates state and covariance one timestep ahead and obtains prior estimations:
// x_prior = A * x, P_prior = A * P * A^t + Q, where A is th process model matrix, which
// is avaliable by its inversion B = A^{-1}.
// \param  x  in: current state; out: prior state estimation.
// \param  P  in: current covariance; out: prior state covariance estimation.
// \param  B  inverse model matrix: B = A^{-1}.
// \param  Q  process noise covariance.
//-------------------------------------------------------------------------------------------------
void PropagateStateInverse(
          VectorView<PROBLEM_SIZE>           & x,
          Matrix<PROBLEM_SIZE, PROBLEM_SIZE> & P,
    const Matrix<PROBLEM_SIZE, PROBLEM_SIZE> & B,
    const Matrix<PROBLEM_SIZE, PROBLEM_SIZE> & Q)
{
    m_x_tmp = x;                    // copy state and covariance into
    m_P_tmp = P;                    // the separate temporary objects

    m_lu.Init(B);                   // decompose: B = L * U
    m_lu.Solve(x, m_x_tmp);         // x_prior = B^{-1} * x

    m_lu.BatchSolve(m_P_tmp, P);    // P_tmp = B^{-1} * P, where P is assumed symmetric!
    m_lu.BatchSolveTr(P, m_P_tmp);  // P_prior = B^{-1} * (B^{-1} * P)^t = A * P * A^t
    AddMatrices(P, P, Q);           // P_prior = A * P * A^t + Q
    Symmetrize(P);                  // correct loss of symmetry due to round-off errors
}

//-------------------------------------------------------------------------------------------------
// Function makes an iteration of Kalman filter given already estimated (prior) state and
// its covariance.
// \param  x  in: prior state estimation; out: posterior state estimation.
// \param  P  in: prior state covariance estimation; out: posterior state covariance estimation.
// \param  H  observation model: z = H * x + v.
// \param  R  measurement noise (v) covariance.
// \param  z  vector of observations.
//-------------------------------------------------------------------------------------------------
void SolveFilter(
              VectorView<PROBLEM_SIZE>                   & x,
              Matrix<PROBLEM_SIZE, PROBLEM_SIZE>         & P,
        const Matrix<NUM_OBSERVATIONS, PROBLEM_SIZE>     & H,
        const Matrix<NUM_OBSERVATIONS, NUM_OBSERVATIONS> & R,
        const VectorView<NUM_OBSERVATIONS>               & z)
{
    m_x_tmp = x;                        // copy state and covariance into
    m_P_tmp = P;                        // the separate temporary objects
    const auto & x_prior = m_x_tmp;     // original state
    const auto & P_prior = m_P_tmp;     // original covariance

    // y = z - H * x_prior
    MatVecMult(m_y, H, x_prior);
    SubtractVectors(m_y, z, m_y);

    // S = H * P_prior * H^t + R
    MatMultTr(m_PHt, P_prior, H);
    MatMult(m_S, H, m_PHt);
    AddMatrices(m_S, m_S, R);

    // Correct symmetry loss due to round-off errors.
    Symmetrize(m_S);

    // Compute Cholesky decomposition  S = L * L^t  to facilitate matrix inversion.
    m_chol.Init(m_S);

    // m_invSy = S^{-1} * y
    m_chol.Solve(m_invSy, m_y);

    // x = x_prior + K * y = x_prior + P_prior * H^t * S^{-1} * y
    MatVecMult(x, m_PHt, m_invSy);
    AddVectors(x, x, x_prior);

    // m_invSHP = S^{-1} * H * P_prior
    GetTransposed(m_HP, m_PHt);
    m_chol.BatchSolve(m_invSHP, m_HP);

    // P = (I - K * H) * P_prior = P_prior - P_prior * H^t * S^{-1} * H * P_prior.
    MatMult(P, m_PHt, m_invSHP);
    SubtractMatrices(P, P_prior, P);

    // Correct symmetry loss due to round-off errors.
    Symmetrize(P);
}

private:
//-------------------------------------------------------------------------------------------------
// Function makes an iteration of Kalman filter given already estimated "x_prior" and "P_prior".
// \param  H  observation model: z_k = H_k * x_k + v_k.
// \param  R  measurement noise (v_k) covariance.
// \param  z  vector of observations.
// \param  x  out: new state.
// \param  P  out: new covariance.
//-------------------------------------------------------------------------------------------------
void PosteriorEstimation(const Matrix<NUM_OBSERVATIONS, PROBLEM_SIZE>     & H,
                         const Matrix<NUM_OBSERVATIONS, NUM_OBSERVATIONS> & R,
                         const VectorView<NUM_OBSERVATIONS>               & z,
                               VectorView<PROBLEM_SIZE>                   & x,
                               Matrix<PROBLEM_SIZE, PROBLEM_SIZE>         & P)
{
    // y = z - H * x_prior
    MatVecMult(m_y, H, m_x_prior);
    SubtractVectors(m_y, z, m_y);

    // S = H * P_prior * H^t + R
    MatMultTr(m_PHt, m_P_prior, H);
    MatMult(m_S, H, m_PHt);
    AddMatrices(m_S, m_S, R);

    // Correct symmetry loss due to round-off errors.
    Symmetrize(m_S);

    // Compute Cholesky decomposition  S = L * L^t  to facilitate matrix inversion.
    m_chol.Init(m_S);

    // m_invSy = S^{-1} * y
    m_chol.Solve(m_invSy, m_y);

    // x  =  x_prior + K * y  =  x_prior + P_prior * H^t * S^{-1} * y
    MatVecMult(x, m_PHt, m_invSy);
    AddVectors(x, x, m_x_prior);

    // m_invSHP = S^{-1} * H * P_prior
    GetTransposed(m_HP, m_PHt);
    m_chol.BatchSolve(m_invSHP, m_HP);

    // P  =  (I - K * H) * P_prior  =  P_prior - P_prior * H^t * S^{-1} * H * P_prior.
    MatMult(P, m_PHt, m_invSHP);
    SubtractMatrices(P, m_P_prior, P);

    // Correct symmetry loss due to round-off errors.
    Symmetrize(P);
}

}; // class KalmanFilter

} // end namespace utils
} // end namespace app
} // end namespace amdados

