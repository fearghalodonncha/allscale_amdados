//-----------------------------------------------------------------------------
// Author    : Albert Akhriev, albert_akhriev@ie.ibm.com
// Copyright : IBM Research Ireland, 2017-2018
//-----------------------------------------------------------------------------

#ifndef AMDADOS_PLAIN_MPI
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <vector>

#include "allscale/utils/assert.h"
#include "allscale/utils/serializer.h"

#include "amdados/app/amdados_utils.h"
#include "amdados/app/matrix.h"
#endif  // AMDADOS_PLAIN_MPI

namespace amdados {

#ifndef AMDADOS_PLAIN_MPI
//-----------------------------------------------------------------------------
// Serialization: load this matrix.
//-----------------------------------------------------------------------------
Matrix Matrix::load(::allscale::utils::ArchiveReader & reader)
{
	Matrix res;
	res.m_nrows = reader.read<index_t>();
	res.m_ncols = reader.read<index_t>();
	res.m_data.resize(static_cast<size_t>(res.m_nrows * res.m_ncols));
	reader.read<double>(res.m_data.data(), res.m_data.size());
	return res;
}
//-----------------------------------------------------------------------------
// Serialization: store this matrix.
//-----------------------------------------------------------------------------
void Matrix::store(::allscale::utils::ArchiveWriter & writer) const
{
	assert_true(m_data.size() == static_cast<size_t>(m_nrows * m_ncols));
	writer.write<index_t>(m_nrows);
	writer.write<index_t>(m_ncols);
	writer.write<double>(m_data.data(), m_data.size());
}
#endif	// AMDADOS_PLAIN_MPI

//-----------------------------------------------------------------------------
// Matrix multiplication: result = A * B.
// @param  result  out: nrows-x-ncols matrix.
// @param  A       nrows-x-msize matrix.
// @param  B       msize-x-ncols matrix.
//-----------------------------------------------------------------------------
void MatMult(Matrix & result, const Matrix & A, const Matrix & B)
{
	const index_t nrows = A.NRows();
	const index_t msize = A.NCols();
	const index_t ncols = B.NCols();
    assert_true(result.IsDistinct(A) && result.IsDistinct(B));
    assert_true((result.NRows() == nrows) &&
                (result.NCols() == ncols) && (msize == B.NRows()));
	for(index_t r = 0; r < nrows; ++r) {
	for(index_t c = 0; c < ncols; ++c) {
        double sum = 0.0;
        for (index_t k = 0; k < msize; ++k) { sum += A(r,k) * B(k,c); }
        result(r,c) = sum;
    }}
}

//-----------------------------------------------------------------------------
// Matrix multiplication with transposition: result = A * B^t.
// Note, the matrix B is only logically but not explicitly transposed.
// @param  result  out: nrows-x-ncols matrix.
// @param  A       nrows-x-msize matrix.
// @param  B       ncols-x-msize matrix.
//-----------------------------------------------------------------------------
void MatMultTr(Matrix & result, const Matrix & A, const Matrix & B)
{
    const index_t nrows = A.NRows();
    const index_t ncols = B.NRows();
    const index_t msize = B.NCols();
    assert_true(result.IsDistinct(A) && result.IsDistinct(B));
    assert_true((result.NRows() == nrows) &&
                (result.NCols() == ncols) && (A.NCols() == msize));
    for (index_t r = 0; r < nrows; ++r) {
    for (index_t c = 0; c < ncols; ++c) {     // get B as transposed
        double sum = 0.0;
        for (index_t k = 0; k < msize; ++k) { sum += A(r,k) * B(c,k); }
        result(r,c) = sum;
    }}
}

//-----------------------------------------------------------------------------
// Matrix-vector multiplication: result = A * v.
//-----------------------------------------------------------------------------
void MatVecMult(Vector & result, const Matrix & A, const Vector & v)
{
    const index_t nrows = A.NRows();
    const index_t ncols = A.NCols();
    assert_true(result.IsDistinct(v));
    assert_true((result.Size() == nrows) && (v.Size() == ncols));
    for (index_t r = 0; r < nrows; ++r) {
        double sum = 0.0;
        for (index_t c = 0; c < ncols; ++c) { sum += A(r,c) * v(c); }
        result(r) = sum;
    }
}

//-----------------------------------------------------------------------------
// Add vectors: result = a + b.
//-----------------------------------------------------------------------------
void AddVectors(Vector & result,
                const Vector & a, const Vector & b)
{
    assert_true(result.SameSize(a) && result.SameSize(b));
    std::transform(a.begin(), a.end(), b.begin(), result.begin(),
                   std::plus<double>());
}

//-----------------------------------------------------------------------------
// Subtract vectors: result = a - b.
//-----------------------------------------------------------------------------
void SubtractVectors(Vector & result,
                     const Vector & a, const Vector & b)
{
    assert_true(result.SameSize(a) && result.SameSize(b));
    std::transform(a.begin(), a.end(), b.begin(), result.begin(),
                   std::minus<double>());
}

//-----------------------------------------------------------------------------
// Add matrices: result = A + B.
//-----------------------------------------------------------------------------
void AddMatrices(Matrix & result, const Matrix & A, const Matrix & B)
{
    assert_true(result.SameSize(A) && result.SameSize(B));
    std::transform(A.begin(), A.end(), B.begin(), result.begin(),
                   std::plus<double>());
}

//-----------------------------------------------------------------------------
// Subtract matrices: result = A - B.
//-----------------------------------------------------------------------------
void SubtractMatrices(Matrix & result, const Matrix & A, const Matrix & B)
{
    assert_true(result.SameSize(A) && result.SameSize(B));
    std::transform(A.begin(), A.end(), B.begin(), result.begin(),
                   std::minus<double>());
}

//-----------------------------------------------------------------------------
// Function initializes the object by the value specified (default is zero).
//-----------------------------------------------------------------------------
void Fill(Vector & v, double vfill)
{
    std::fill(v.begin(), v.end(), vfill);
}

//-----------------------------------------------------------------------------
// Function initializes the identity matrix.
//-----------------------------------------------------------------------------
void MakeIdentityMatrix(Matrix & A)
{
    std::fill(A.begin(), A.end(), 0.0);
    for (index_t i = 0; i < A.NRows(); ++i) { A(i,i) = 1.0; }
}

//-----------------------------------------------------------------------------
// Function computes transposed matrix.
//-----------------------------------------------------------------------------
void GetTransposed(Matrix & At, const Matrix & A)
{
    const index_t nrows = A.NRows();
    const index_t ncols = A.NCols();
    assert_true(At.IsDistinct(A) && A.SameSizeTr(At));
    for (index_t r = 0; r < nrows; ++r) {
    for (index_t c = 0; c < ncols; ++c) { At(c,r) = A(r,c); }}
}

//-----------------------------------------------------------------------------
// Due to round-off errors a matrix supposed to be symmetric can loose this
// property. The function brings the matrix back to symmetry.
//-----------------------------------------------------------------------------
void Symmetrize(Matrix & A)
{
    const index_t nrows = A.NRows();
    assert_true(A.IsSquare());
    for (index_t i = 0;     i < nrows; ++i) {
    for (index_t j = i + 1; j < nrows; ++j) {
        A(j,i) = A(i,j) = 0.5 * (A(j,i) + A(i,j));
    }}
}

//-----------------------------------------------------------------------------
// Multiplying object by a scalar: v = v * mult.
//-----------------------------------------------------------------------------
void ScalarMult(Vector & v, const double mult)
{
    std::transform(v.begin(), v.end(), v.begin(),
                    [mult](double x) { return x*mult; });
}

//-----------------------------------------------------------------------------
// L2 norm of an object |a|; Frobenius norm for matrices.
//-----------------------------------------------------------------------------
double Norm(const Vector & v)
{
    double sum = 0.0;
    std::for_each(v.begin(), v.end(), [&](double x) { sum += x*x; });
    return std::sqrt(std::fabs(sum));
}

//-----------------------------------------------------------------------------
// L2 norm of objects difference: |a - b|; Frobenius norm for matrices.
//-----------------------------------------------------------------------------
double NormDiff(const Vector & a, const Vector & b)
{
    assert_true(a.SameSize(b));     // weak verification in case of matrices
    double sum = 0.0;
    auto ia = a.begin(), ib = b.begin();
    for (; ia != a.end(); ++ia, ++ib) { sum += std::pow(*ia - *ib, 2); }
    assert_true(ib == b.end());
    return std::sqrt(std::fabs(sum));
}

//-----------------------------------------------------------------------------
// Function returns the trace of a square matrix.
//-----------------------------------------------------------------------------
double Trace(const Matrix & A)
{
    const index_t nrows = A.NRows();
    assert_true(A.IsSquare());
    double sum = 0.0;
    for (index_t i = 0; i < nrows; ++i) { sum += A(i,i); }
    return sum;
}

//-----------------------------------------------------------------------------
// Change vector/matrix sign in-place: v = -v.
//-----------------------------------------------------------------------------
void Negate(Vector & v)
{
    const index_t size = v.Size();
    for (index_t i = 0; i < size; ++i) { v(i) = - v(i); }
}

//-----------------------------------------------------------------------------
// Function generates a random object with either normal (mu=0, sigma=1) or
// uniform (0..1) distribution of entry values.
//-----------------------------------------------------------------------------
void MakeRandom(Vector & v, const char type)
{
    const index_t size = v.Size();
    std::mt19937_64 gen(RandomSeed());
    if (type == 'n') {                              // normal, mu=0, sigma=1
        std::normal_distribution<double> distrib;
        for (index_t i = 0; i < size; ++i) { v(i) = distrib(gen); }
    } else if (type == 'u') {                       // uniform, [0..1]
        std::uniform_real_distribution<double> distrib;
        for (index_t i = 0; i < size; ++i) { v(i) = distrib(gen); }
    } else {
        assert_true(0) << "unknown distribution" << "\n";
    }
}

//-----------------------------------------------------------------------------
// Function checks there is no NAN values among vector/matrix entries.
//-----------------------------------------------------------------------------
bool CheckNoNan(const Vector & v)
{
    for (auto i = v.begin(); i != v.end(); ++i) {
        if (std::isnan(*i))
            return false;
    }
    return true;
}

} // namespace amdados

