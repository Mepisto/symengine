#include <algorithm>
#include <typeinfo>

#include "matrix.h"
#include "add.h"
#include "mul.h"
#include "integer.h"

namespace CSymPy {
// ----------------------------- CSRMatrix ------------------------------------
CSRMatrix::CSRMatrix(unsigned row, unsigned col)
        : MatrixBase(row, col)
{
}

CSRMatrix::CSRMatrix(unsigned row, unsigned col, std::vector<unsigned>&& p,
    std::vector<unsigned>&& j, vec_basic&& x)
        : MatrixBase(row, col), p_{std::move(p)}, j_{std::move(j)}, x_{std::move(x)}
{
    CSYMPY_ASSERT(is_canonical());
}

bool CSRMatrix::eq(const MatrixBase &other) const
{
    unsigned row = this->nrows();
    if (row != other.nrows() || this->ncols() != other.ncols())
        return false;

    if (typeid(*this) == typeid(other)) {
        const CSRMatrix &o = dynamic_cast<const CSRMatrix &>(other);

        if (this->p_[row] != o.p_[row])
            return false;

        for (unsigned i = 0; i <= row; i++)
            if(this->p_[i] != o.p_[i])
                return false;

        for (unsigned i = 0; i < this->p_[row]; i++)
            if ((this->j_[i] != o.j_[i]) || neq(this->x_[i], o.x_[i]))
                return false;

        return true;
    } else {
        return this->MatrixBase::eq(other);
    }
}

bool CSRMatrix::is_canonical()
{
    if (p_.size() != row_ + 1 || j_.size() != p_[row_] || x_.size() != p_[row_])
        return false;
    return csr_has_canonical_format(p_, j_, row_);
}

// Get and set elements
RCP<const Basic> CSRMatrix::get(unsigned i) const
{
    unsigned row = i / col_;
    unsigned col = i - row*col_;

    unsigned row_start = p_[row];
    unsigned row_end = p_[row + 1] - 1;

    for (unsigned j = row_start; j <= row_end; j++) {
        if (j_[j] == col)
            return x_[j];
        else if (j_[j] > col)
            break;
    }

    return zero;
}

void CSRMatrix::set(unsigned i, RCP<const Basic> &e)
{
}

unsigned CSRMatrix::rank() const
{
    throw std::runtime_error("Not implemented.");
}

RCP<const Basic> CSRMatrix::det() const
{
    throw std::runtime_error("Not implemented.");
}

RCP<const MatrixBase> CSRMatrix::inv() const
{
    throw std::runtime_error("Not implemented.");
}

MatrixBase& CSRMatrix::add_matrix(const MatrixBase &other) const
{
    throw std::runtime_error("Not implemented.");
}

MatrixBase& CSRMatrix::mul_matrix(const MatrixBase &other) const
{
    throw std::runtime_error("Not implemented.");
}

void CSRMatrix::csr_sum_duplicates(std::vector<unsigned>& p_,
	std::vector<unsigned>& j_,
    vec_basic& x_,
    unsigned row_)
{
    unsigned nnz = 0;
    unsigned row_end = 0;
    unsigned jj = 0, j = 0;
    RCP<const Basic> x = zero;

    for (unsigned i = 0; i < row_; i++) {
        jj = row_end;
        row_end = p_[i + 1];

        while (jj < row_end) {
            j = j_[jj];
            x = x_[jj];
            jj++;

            while (jj < row_end && j_[jj] == j) {
                x = add(x, x_[jj]);
                jj++;
            }

            j_[nnz] = j;
            x_[nnz] = x;
            nnz++;
        }
        p_[i + 1] = nnz;
    }

    // Resize to discard unnecessary elements
    j_.resize(nnz);
    x_.resize(nnz);
}

void CSRMatrix::csr_sort_indices(std::vector<unsigned>& p_,
	std::vector<unsigned>& j_,
	vec_basic& x_,
    unsigned row_)
{
    std::vector< std::pair<unsigned, RCP<const Basic>> > temp;

    for (unsigned i = 0; i < row_; i++) {
        unsigned row_start = p_[i];
        unsigned row_end   = p_[i + 1];

        temp.clear();

        for(unsigned jj = row_start; jj < row_end; jj++){
            temp.push_back(std::make_pair(j_[jj], x_[jj]));
        }

        std::sort(temp.begin(), temp.end(),
        	[](const std::pair<unsigned, RCP<const Basic>>& x,
        		const std::pair<unsigned, RCP<const Basic>>& y) {
        		return x.first < y.first;
        	}
		);

        for(unsigned jj = row_start, n = 0; jj < row_end; jj++, n++){
            j_[jj] = temp[n].first;
            x_[jj] = temp[n].second;
        }
    }
}

// Assumes that the indices are sorted
bool CSRMatrix::csr_has_duplicates(const std::vector<unsigned>& p_,
    const std::vector<unsigned>& j_,
    unsigned row_)
{
    for (unsigned i = 0; i < row_; i++) {
        for (unsigned j = p_[i]; j < p_[i + 1] - 1; j++) {
            if (j_[j] == j_[j + 1])
                return true;
        }
    }
    return false;
}

bool CSRMatrix::csr_has_sorted_indices(const std::vector<unsigned>& p_,
	const std::vector<unsigned>& j_,
    unsigned row_)
{
    for (unsigned i = 0; i < row_; i++) {
        for (unsigned jj = p_[i]; jj < p_[i + 1] - 1; jj++) {
            if (j_[jj] > j_[jj + 1])
                return false;
        }
    }
    return true;
}

bool CSRMatrix::csr_has_canonical_format(const std::vector<unsigned>& p_,
	const std::vector<unsigned>& j_,
    unsigned row_)
{
    for (unsigned i = 0; i < row_; i++) {
        if (p_[i] > p_[i + 1])
            return false;
    }
    return csr_has_sorted_indices(p_, j_, row_) && !csr_has_duplicates(p_, j_, row_);
}

CSRMatrix CSRMatrix::from_coo(unsigned row, unsigned col,
    const std::vector<unsigned>& i, const std::vector<unsigned>& j,
    const vec_basic& x)
{
    unsigned nnz = x.size();
    std::vector<unsigned> p_ = std::vector<unsigned>(row + 1, 0);
    std::vector<unsigned> j_ = std::vector<unsigned>(nnz);
    vec_basic x_ = vec_basic(nnz);

    for (unsigned n = 0; n < nnz; n++) {
        p_[i[n]]++;
    }

    // cumsum the nnz per row to get p
    unsigned temp;
    for (unsigned i = 0, cumsum = 0; i < row; i++) {
        temp = p_[i];
        p_[i] = cumsum;
        cumsum += temp;
    }
    p_[row] = nnz;

    // write j, x into j_, x_
    unsigned row_, dest_;
    for (unsigned n = 0; n < nnz; n++) {
        row_  = i[n];
        dest_ = p_[row_];

        j_[dest_] = j[n];
        x_[dest_] = x[n];

        p_[row_]++;
    }

    for (unsigned i = 0, last = 0; i <= row; i++) {
        std::swap(p_[i], last);
    }

    // sort indices
    csr_sort_indices(p_, j_, x_, row);
    // Remove duplicates
    csr_sum_duplicates(p_, j_, x_, row);

    CSRMatrix B = CSRMatrix(row, col, std::move(p_), std::move(j_), std::move(x_));
    return B;
}

void csr_matmat_pass1(const CSRMatrix &A, const CSRMatrix &B, CSRMatrix &C)
{
    // method that uses O(n) temp storage
    std::vector<unsigned> mask(A.col_, -1);
    C.p_[0] = 0;

    unsigned nnz = 0;
    for (unsigned i = 0; i < A.row_; i++) {
        // npy_intp row_nnz = 0;
        unsigned row_nnz = 0;

        for (unsigned jj = A.p_[i]; jj < A.p_[i + 1]; jj++) {
            unsigned j = A.j_[jj];
            for (unsigned kk = B.p_[j]; kk < B.p_[j + 1]; kk++) {
                unsigned k = B.j_[kk];
                if(mask[k] != i){
                    mask[k] = i;
                    row_nnz++;
                }
            }
        }

        unsigned next_nnz = nnz + row_nnz;

        // Addition overflow: http://www.cplusplus.com/articles/DE18T05o/
        if (next_nnz < nnz) {
            throw std::overflow_error("nnz of the result is too large");
        }

        nnz = next_nnz;
        C.p_[i + 1] = nnz;
    }
}


// Pass 2 computes CSR entries for matrix C = A*B using the
// row pointer Cp[] computed in Pass 1.
void csr_matmat_pass2(const CSRMatrix &A, const CSRMatrix &B, CSRMatrix &C)
{
    std::vector<int> next(A.col_, -1);
    vec_basic sums(A.col_, zero);

    unsigned nnz = 0;

    C.p_[0] = 0;

    for (unsigned i = 0; i < A.row_; i++) {
        int head   = -2;
        unsigned length =  0;

        unsigned jj_start = A.p_[i];
        unsigned jj_end   = A.p_[i + 1];
        for (unsigned jj = jj_start; jj < jj_end; jj++) {
            unsigned j = A.j_[jj];
            RCP<const Basic> v = A.x_[jj];

            unsigned kk_start = B.p_[j];
            unsigned kk_end   = B.p_[j + 1];
            for (unsigned kk = kk_start; kk < kk_end; kk++) {
                unsigned k = B.j_[kk];

                sums[k] = add(sums[k], mul(v, B.x_[kk]));

                if (next[k] == -1) {
                    next[k] = head;
                    head  = k;
                    length++;
                }
            }
        }

        for (unsigned jj = 0; jj < length; jj++) {

            if (neq(sums[head], zero)) {
                C.j_[nnz] = head;
                C.x_[nnz] = sums[head];
                nnz++;
            }

            unsigned temp = head;
            head = next[head];

            next[temp] = -1; //clear arrays
            sums[temp] =  zero;
        }

        C.p_[i + 1] = nnz;
    }
}

// Extract main diagonal of CSR matrix A
void csr_diagonal(const CSRMatrix& A, DenseMatrix& D)
{
    unsigned N = std::min(A.row_, A.col_);

    CSYMPY_ASSERT(D.nrows() == N && D.ncols() == 1);

    unsigned row_start;
    unsigned row_end;
    RCP<const Basic> diag;

    for (unsigned i = 0; i < N; i++) {
        row_start = A.p_[i];
        row_end = A.p_[i + 1];
        diag = zero;

        // TODO: Use Binary search as A is in canonical format
        for (unsigned jj = row_start; jj < row_end; jj++) {
            if (A.j_[jj] == i) {
                diag = A.x_[jj];
                break;
            }
        }
        D.set(i, diag);
    }
}

// Scale the rows of a CSR matrix *in place*
// A[i, :] *= X[i]
void csr_scale_rows(CSRMatrix& A, const DenseMatrix& X)
{
    CSYMPY_ASSERT(A.row_ == X.nrows() && X.ncols() == 1);

    for(unsigned i = 0; i < A.row_; i++) {
        if (eq(X.get(i), zero))
            throw std::runtime_error("Scaling factor can't be zero");
        for (unsigned jj = A.p_[i]; jj < A.p_[i + 1]; jj++)
            A.x_[jj] = mul(A.x_[jj], X.get(i));
    }
}

// Scale the columns of a CSR matrix *in place*
// A[:,i] *= X[i]
void csr_scale_columns(CSRMatrix& A, const DenseMatrix& X)
{
    CSYMPY_ASSERT(A.col_ == X.nrows() && X.ncols() == 1);

    const unsigned nnz = A.p_[A.row_];
    unsigned i;

    for (i = 0; i < A.col_; i++) {
        if (eq(X.get(i), zero))
            throw std::runtime_error("Scaling factor can't be zero");
    }

    for (i = 0; i < nnz; i++)
        A.x_[i] = mul(A.x_[i], X.get(A.j_[i]));
}

} // CSymPy
