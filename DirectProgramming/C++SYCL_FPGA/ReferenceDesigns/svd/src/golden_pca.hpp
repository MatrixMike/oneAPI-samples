#include <math.h>

#include <fstream>
#include <iomanip>
#include <random>

/*
This file implements the steps to
identify principal components (Eigenvectors)
of a matrix and finally transform input matrix
along the directions of the principal components

Following are the main steps in order to transform an
matrix A made of multiple samples with different features.
The matrix A will contain n samples with p features
making it an n row, p columns matrix

Here are the steps performed by this file:
1. Compute the covariance matrices of these matrices
2. Compute Eigenvectors and Eigenvalues of the covariance matrices using the
QR iteration method

*/
template <typename T>
class GoldenPCA {
 public:
  int samples;                       // number of samples
  int features;                      // number of features
  int matrix_count;                  // number of matrices
  bool debug;                        // print debug information if true
  bool benchmark_mode;               // pull data from actual dataset
  std::vector<T> a_matrix;           // storage for input matrices
  std::vector<T> covariance_matrix;  // storage for covariance matrices
  std::vector<T> eigenvalues;       // storage for the Eigenvalues
  std::vector<T> eigenvectors;      // storage for the Eigenvectors
  std::vector<T> iterations;         // the number of QR iterations per matrix

 private:
  std::default_random_engine gen;

 public:
  // Constructor
  GoldenPCA(int n, int p, int count, bool d, bool benchmark,
            std::vector<std::vector<T> > &input_mat) {
    samples = n;
    features = p;
    benchmark_mode = benchmark;
    matrix_count = count;
    debug = d;

    a_matrix.resize(n * p * matrix_count);
    for (int r = 0; r < n; r++) {
      for (int c = 0; c < p; c++) {
        a_matrix[r * p + c] = input_mat[r][c];
      }
    }

    covariance_matrix.resize(p * p * matrix_count);

    eigenvalues.resize(p * matrix_count);
    eigenvectors.resize(p * p * matrix_count);

    iterations.resize(matrix_count);
  }

  // Compute the covariance matrix of the matrix with index matrix_index
  void ComputeCovarianceIthMatrix(int matrix_index) {
    // The covariance matrix is defined as the product of the
    // transposition of A by A. This matrix product then needs to be divided
    // by the number of samples-1.
    // This will result in a matrix of size features x features

    if (debug)
      std::cout << "\nCovariance matrix #" << matrix_index << std::endl;
    int a_matrix_offset = matrix_index * samples * features;
    int matrix_c_offset = matrix_index * features * features;
    for (int row = 0; row < features; row++) {
      for (int column = 0; column < features; column++) {
        double dot_product = 0;
        for (int k = 0; k < samples; k++) {
          dot_product += a_matrix[a_matrix_offset + k * features + row] *
                         a_matrix[a_matrix_offset + k * features + column];
        }
        covariance_matrix[matrix_c_offset + row * features + column] =
            dot_product;
        if (debug)
          std::cout
              << covariance_matrix[matrix_c_offset + row * features + column]
              << " ";
      }
      if (debug) std::cout << std::endl;
    }

    if (debug) {
      std::cout << "Cov=[";
      for (int row = 0; row < features; row++) {
        for (int column = 0; column < features; column++) {
          std::cout
              << covariance_matrix[matrix_c_offset + row * features + column]
              << " ";
        }
        if (row != (features - 1)) {
          std::cout << "; ";
        }
      }
      std::cout << "]" << std::endl;
    }
  }

  // Compute the covariance matrix of all the standardized A matrices
  void ComputeCovarianceMatrix() {
    for (int matrix_index = 0; matrix_index < matrix_count; matrix_index++) {
      ComputeCovarianceIthMatrix(matrix_index);
    }
  }

  // Compute the covariance matrix of the standardized A matrix
  void ComputeEigenValuesAndVectors() {
    // Compute the Eigenvalues and Eigenvectors using the QR iteration method
    // This implementation uses the Wilkinson shift to speedup the convergence

    constexpr float kZeroThreshold = 1e-8;

    for (int matrix_index = 0; matrix_index < matrix_count; matrix_index++) {
      if (debug)
        std::cout << "\nComputing Eigenvalues and vectors of matrix #"
                  << matrix_index << std::endl;

      int offset = matrix_index * features * features;

      // Compute the QR decomposition of the current matrix
      std::vector<double> q, r, rq;
      q.resize(features * features);
      r.resize(features * features);
      rq.resize(features * features);

      // Copy the covariance matrix into the input matrix to the QR
      // decomposition
      for (int k = 0; k < features * features; k++) {
        rq[k] = covariance_matrix[offset + k];
      }

      // Initialize the Eigenvectors matrix to the identity matrix
      for (int row = 0; row < features; row++) {
        for (int column = 0; column < features; column++) {
          eigenvectors[offset + row * features + column] =
              row == column ? 1 : 0;
        }
      }

      // Count the number of iterations to abort if there is no convergence
      int iterations = 0;
      bool converged = false;
      while (!converged) {
        // Compute the shift value of the current matrix
        double shift_value = 0;

        // First find where the shift should be applied
        // Start from the last submatrix
        int shift_row = features - 2;
        for (int row = features - 1; row >= 1; row--) {
          bool row_is_zero = true;
          for (int col = 0; col < row; col++) {
            row_is_zero &= (fabs(rq[row * features + col]) < kZeroThreshold);
          }
          if (!row_is_zero) {
            break;
          }
          shift_row--;
        }

        if (shift_row >= 0) {
          // Compute the shift value
          // Take the submatrix:
          // [a b]
          // [b c]
          // and compute the shift such as
          // mu = c - (sign(d)* b*b)/(abs(d) + sqrt(d*d + b*b))
          // where d = (a - c)/2

          double a = rq[shift_row + features * shift_row];
          double b = rq[shift_row + features * (shift_row + 1)];
          double c = rq[(shift_row + 1) + features * (shift_row + 1)];

          double d = (a - c) / 2;
          double b_squared = b * b;
          double d_squared = d * d;
          double b_squared_signed = d < 0 ? -b_squared : b_squared;
          shift_value =
              c - b_squared_signed / (abs(d) + sqrt(d_squared + b_squared));
        }

        // Use the 99% percentage of the shift value to avoid
        // massive cancellations in the QRD
        if (iterations == 0) {
          shift_value = 0;
        } else {
          shift_value *= 0.99;
        }

        // Subtract the shift value from the diagonal of RQ
        for (int row = 0; row < features; row++) {
          rq[row + features * row] -= shift_value;
        }

        // Compute the actual QR decomposition
        for (int row = 0; row < features; row++) {
          for (int column = 0; column < features; column++) {
            r[row * features + column] = 0;
            q[row * features + column] = 0;
          }
        }
        for (int i = 0; i < features; i++) {
          double norm = 0;
          for (int k = 0; k < features; k++) {
            norm += double(rq[k * features + i]) * double(rq[k * features + i]);
          }
          double rii = std::sqrt(norm);
          r[i * features + i] = rii;  // r_ii = ||a_i||

          for (int k = 0; k < features; k++) {
            q[k * features + i] = rq[k * features + i] / rii;
          }

          for (int j = i + 1; j < features; j++) {
            double dp = 0;
            for (int k = 0; k < features; k++) {
              dp += q[k * features + i] * rq[k * features + j];
            }
            r[i * features + j] = dp;

            for (int k = 0; k < features; k++) {
              rq[k * features + j] -= (dp * q[k * features + i]);
            }
          }
        }

        // Compute the updated Eigenvectors
        std::vector<T> eigenvectors_q_product;
        eigenvectors_q_product.resize(features * features);
        for (int row = 0; row < features; row++) {
          for (int col = 0; col < features; col++) {
            double prod = 0;
            for (int k = 0; k < features; k++) {
              prod += eigenvectors[offset + row * features + k] *
                      q[k * features + col];
            }
            eigenvectors_q_product[row * features + col] = prod;
          }
        }

        for (int row = 0; row < features; row++) {
          for (int col = 0; col < features; col++) {
            eigenvectors[offset + row * features + col] =
                eigenvectors_q_product[row * features + col];
          }
        }

        for (int row = 0; row < features; row++) {
          for (int col = 0; col < features; col++) {
            double prod = 0;
            for (int k = 0; k < features; k++) {
              prod += r[row * features + k] * q[k * features + col];
            }
            rq[row * features + col] = prod;
          }
        }

        // Add the shift value back to the diagonal of RQ
        for (int row = 0; row < features; row++) {
          rq[row + features * row] += shift_value;
        }

        // Check if we found all Eigenvalues
        bool all_below_threshold = true;
        for (int row = 1; row < features; row++) {
          for (int col = 0; col < row; col++) {
            all_below_threshold &=
                (std::fabs(rq[row * features + col]) < kZeroThreshold);
          }
        }
        converged = all_below_threshold;

        iterations++;
        if ((iterations > (features * features * 16)) && !benchmark_mode) {
          std::cout << "Number of iterations too high" << std::endl;
          break;
        }
      }

      if (debug)
        std::cout << "QR iteration stopped after " << iterations
                  << " iterations" << std::endl;
      this->iterations[matrix_index] = iterations;
      if (debug)
        std::cout << "Eigenvalues for matrix #" << matrix_index << std::endl;
      for (int k = 0; k < features; k++) {
        eigenvalues[k + matrix_index * features] = rq[k + features * k];
        if (debug) std::cout << rq[k + features * k] << " ";
      }
      if (debug) std::cout << std::endl;

      if (debug) {
        std::cout << "Eigenvectors for matrix #" << matrix_index << std::endl;
        for (int row = 0; row < features; row++) {
          for (int col = 0; col < features; col++) {
            std::cout << eigenvectors[offset + row * features + col] << " ";
          }
          std::cout << std::endl;
        }
        std::cout << std::endl;
      }
    }  // end for:matrix_index
  }

};  // class PCA