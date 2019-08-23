#include "BFIData.h"
#include "ImageUtils.h"

USING_NS_BF;

#define IS_ZERO(v) ((fabs(v) < 0.000000001))

#include <complex>
#include <iostream>
#include <valarray>
 
const double PI = 3.141592653589793238460;
 
typedef std::complex<double> Complex;
typedef std::valarray<Complex> CArray;
 
// Cooley�Tukey FFT (in-place)
void fft(CArray& x)
{
    const size_t N = x.size();
    if (N <= 1) return;
 
    // divide
    CArray even = x[std::slice(0, N/2, 2)];
    CArray  odd = x[std::slice(1, N/2, 2)];
 
    // conquer
    fft(even);
    fft(odd);
 
    // combine
    for (size_t k = 0; k < N/2; ++k)
    {
        Complex t = std::polar(1.0, -2 * PI * k / N) * odd[k];
        x[k    ] = even[k] + t;
        x[k+N/2] = even[k] - t;
    }
}
 
void DFTTest()
{
	const Complex test[] = { 1.0, 2.0, 3.0, 4.0 };
    CArray data(test, sizeof(test) / sizeof(test[0]));
 
    fft(data);
	
}

/*-------------------------------------------------------------------------
   Perform a 2D FFT inplace given a complex 2D array
   The direction dir, 1 for forward, -1 for reverse
   The size of the array (nx,ny)
   Return false if there are memory problems or
      the dimensions are not powers of 2
*/

class COMPLEX
{
public:
	double real;
	double imag;
};


/*-------------------------------------------------------------------------
   This computes an in-place complex-to-complex FFT
   x and y are the real and imaginary arrays of 2^m points.
   dir =  1 gives forward transform
   dir = -1 gives reverse transform

     Formula: forward
                  N-1
                  ---
              1   \          - j k 2 pi n / N
      X(n) = ---   >   x(k) e                    = forward transform
              N   /                                n=0..N-1
                  ---
                  k=0

      Formula: reverse
                  N-1
                  ---
                  \          j k 2 pi n / N
      X(n) =       >   x(k) e                    = forward transform
                  /                                n=0..N-1
                  ---
                  k=0
*/
int FFT(int dir,int m,double *x,double *y)
{
   long nn,i,i1,j,k,i2,l,l1,l2;
   double c1,c2,tx,ty,t1,t2,u1,u2,z;

   /* Calculate the number of points */
   nn = 1;
   for (i=0;i<m;i++)
      nn *= 2;

   /* Do the bit reversal */
   i2 = nn >> 1;
   j = 0;
   for (i=0;i<nn-1;i++) {
      if (i < j) {
         tx = x[i];
         ty = y[i];
         x[i] = x[j];
         y[i] = y[j];
         x[j] = tx;
         y[j] = ty;
      }
      k = i2;
      while (k <= j) {
         j -= k;
         k >>= 1;
      }
      j += k;
   }

   /* Compute the FFT */
   c1 = -1.0;
   c2 = 0.0;
   l2 = 1;
   for (l=0;l<m;l++) {
      l1 = l2;
      l2 <<= 1;
      u1 = 1.0;
      u2 = 0.0;
      for (j=0;j<l1;j++) {
         for (i=j;i<nn;i+=l2) {
            i1 = i + l1;
            t1 = u1 * x[i1] - u2 * y[i1];
            t2 = u1 * y[i1] + u2 * x[i1];
            x[i1] = x[i] - t1;
            y[i1] = y[i] - t2;
            x[i] += t1;
            y[i] += t2;
         }
         z =  u1 * c1 - u2 * c2;
         u2 = u1 * c2 + u2 * c1;
         u1 = z;
      }
      c2 = sqrt((1.0 - c1) / 2.0);
      if (dir == 1)
         c2 = -c2;
      c1 = sqrt((1.0 + c1) / 2.0);
   }

   /* Scaling for forward transform */
   if (dir == 1) {
      for (i=0;i<nn;i++) {
         x[i] /= (double)nn;
         y[i] /= (double)nn;
      }
   }

   return true;
}

class BFComplex
{
public:
	double mR;
	double mI;
	
public:
	BFComplex(double r = 0, double i = 0)
	{
		mR = r;
		mI = i;		
	}

	BFComplex operator +(const BFComplex& complex) const
	{
		return BFComplex(mR + complex.mR, mI + complex.mI);
	}

	BFComplex& operator +=(const BFComplex& complex)
	{
		*this = BFComplex(mR + complex.mR, mI + complex.mI);
		return *this;
	}

	BFComplex operator *(const BFComplex& complex) const
	{
		return BFComplex(mR * complex.mR - mI * complex.mI, mR * complex.mI + mI * complex.mR);
	}

	BFComplex operator *(double scalar) const
	{
		return BFComplex(mR * scalar, mI * scalar);
	}

	double Magnitude()
	{
		return sqrt(mR * mR + mI * mI);
	}

	double Phase()
	{
		return atan2(mI, mR);
	}

	String ToString()
	{
		if (fabs(mI) < 0.00000001)
		{
			if (fabs(mR) < 0.00000001)
				return StrFormat("%f", 0.0);
			return StrFormat("%f", mR);
		}
		if (mI > 0)
			return StrFormat("%f + %fi", mR, mI);
		return StrFormat("%f - %fi", mR, -mI);
	}

	static BFComplex Polar(double scalar, double angle)
	{
		return BFComplex(scalar * cos(angle), scalar * sin(angle));
	}
};

bool Powerof2(int num, int* power, int* twoPM)
{
	*power = 0;
	*twoPM = 1;
	while (*twoPM < num)
	{
		*twoPM *= 2;
		(*power)++;
	}

	return *twoPM == num;
}

int FFT2D(BFComplex **c,int nx,int ny,int dir)
{
   int i,j;
   int m,twopm;
   double *real,*imag;
   

   /* Transform the rows */
   real = (double *)malloc(nx * sizeof(double));
   imag = (double *)malloc(nx * sizeof(double));
   if (real == NULL || imag == NULL)
      return(false);
   if (!Powerof2(nx,&m,&twopm) || twopm != nx)
      return(false);
   for (j=0;j<ny;j++) {
      for (i=0;i<nx;i++) {
         real[i] = c[i][j].mR;
         imag[i] = c[i][j].mI;
      }	  
      FFT(dir,m,real,imag);
      for (i=0;i<nx;i++) {
         c[i][j].mR = real[i];
         c[i][j].mI = imag[i];
      }
   }
   free(real);
   free(imag);

   /* Transform the columns */
   real = (double *)malloc(ny * sizeof(double));
   imag = (double *)malloc(ny * sizeof(double));
   if (real == NULL || imag == NULL)
      return(false);
   if (!Powerof2(ny,&m,&twopm) || twopm != ny)
      return(false);
   for (i=0;i<nx;i++) {
      for (j=0;j<ny;j++) {
         real[j] = c[i][j].mR;
         imag[j] = c[i][j].mI;
      }	  
      FFT(dir,m,real,imag);
      for (j=0;j<ny;j++) {
         c[i][j].mR = real[j];
         c[i][j].mI = imag[j];
      }
   }
   free(real);
   free(imag);

   return(true);
}


void DFTTest_2D()
{
	BFComplex** test = new BFComplex*[4];
	for (int i = 0; i < 4; i++)
	{
		test[i] = new BFComplex[4];
		for (int j = 0; j < 4; j++)
			test[i][j].mR = i*4+j+1;
	}
	
	/*std::wstring aString;	
	for (int i = 0; i < 4; i++)
	{		
		aString += L"(";
		for (int j = 0; j < 4; j++)
		{
			if (j != 0)
				aString += L", ";
			aString += test[i][j].ToString();
		}
		aString += L")\n";
	}
	OutputDebugStringW(aString.c_str());*/

	/*[][3] = 
	{{1.0, 2.0, 3.0}, 
	{4.0, 5.0, 6.0},
	{7.0, 8.0, 9.0}};*/

	FFT2D(test, 4, 4, -1);

	String aString;	
	for (int i = 0; i < 4; i++)
	{		
		aString += "(";
		for (int j = 0; j < 4; j++)
		{
			if (j != 0)
				aString += ", ";
			aString += test[i][j].ToString();
		}
		aString += ")\n";
	}
    BfpOutput_DebugString(aString.c_str());

	
}

void PrintComplex2D(BFComplex* ptr, int cols, int rows)
{
	String aString = "(\n";	
	for (int i = 0; i < rows; i++)
	{		
		aString += "\t(";
		for (int j = 0; j < cols; j++)
		{
			if (j != 0)
				aString += ", ";
			aString += ptr[i*cols+j].ToString();
		}
		aString += ")\n";
	}
	aString += ")\n";
    BfpOutput_DebugString(aString.c_str());
}


void FFD_1D(BFComplex* array, int size, int pitch, int aDir)
{
	BFComplex* prev = new BFComplex[size];
	for (int i = 0; i < size; i++)
		prev[i] = array[i*pitch];
	
	for (int idxOut = 0; idxOut < size; idxOut++)
	{		
		BFComplex val;
		for (int idxIn = 0; idxIn < size; idxIn++)
			val += prev[idxIn] * BFComplex::Polar(1.0, aDir * 2 * BF_PI_D * idxIn * idxOut / (double) size);		
		if (aDir == 1)
			val = val * BFComplex(1.0/size);
		array[idxOut*pitch] = val;
	}
}

void DFTTest_Mine()
{
	BFComplex test[] = 
	{ 1.0, 2.0, 3.0, 4.0,
	5.0, 6.0, 7.0, 8.0,
	9.0, 10.0, 11.0, 12.0,
	13.0, 14.0, 15.0, 16.0};

	int aCols = 4;
	int aRows = 4;

	/*std::vector<BFComplex> outR;

	for (int row = 0; row < aRows; row++)
	{
		for (int colOut = 0; colOut < aCols; colOut++)
		{
			BFComplex val;
			for (int colIn = 0; colIn < aCols; colIn++)			
				val += test[row*aCols+colIn] * BFComplex::Polar(1.0, - 2 * BF_PI_D * colIn * colOut / (double) aCols);		
			outR.push_back(val);
		}
	}

	PrintComplex2D(&outR.front(), aCols, aRows);
	std::vector<BFComplex> outC;
	outC.insert(outC.begin(), aCols*aRows, BFComplex());

	for (int col = 0; col < aCols; col++)
	{
		for (int rowOut = 0; rowOut < aRows; rowOut++)
		{
			BFComplex val;
			for (int rowIn = 0; rowIn < aRows; rowIn++)			
				val += outR[rowIn*aCols+col] * BFComplex::Polar(1.0, - 2 * BF_PI_D * rowIn * rowOut / (double) aRows);		
			//outC.push_back(val);
			outC[rowOut*aCols+col] = val;
		}
	}*/

	for (int col = 0; col < aCols; col++)
		FFD_1D(test + col, aRows, aCols, -1);
	PrintComplex2D(test, aRows, aCols);
	for (int row = 0; row < aRows; row++)
		FFD_1D(test + row*aCols, aCols, 1, -1);
	PrintComplex2D(test, aRows, aCols);

	/*std::vector<BFComplex> outC;
	outC.insert(outC.begin(), aRows*aCols, BFComplex());

	for (int col = 0; col < aCols; col++)
	{
		for (int rowOut = 0; rowOut < aRows; rowOut++)
		{
			BFComplex val;
			for (int rowIn = 0; rowIn < aRows; rowIn++)			
				val += test[rowIn*aCols+col] * BFComplex::Polar(1.0, - 2 * BF_PI_D * rowIn * rowOut / (double) aRows);		
			outC[rowOut*aCols+col] = val;
		}
	}

	

	PrintComplex2D(&outC.front(), aRows, aCols);
	std::vector<BFComplex> outR;
	outR.insert(outR.begin(), aRows*aCols, BFComplex());

	for (int row = 0; row < aRows; row++)
	{
		for (int colOut = 0; colOut < aCols; colOut++)
		{
			BFComplex val;
			for (int colIn = 0; colIn < aCols; colIn++)			
				val += outC[row*aCols+colIn] * BFComplex::Polar(1.0, - 2 * BF_PI_D * colIn * colOut / (double) aCols);		
			//outC.push_back(val);
			outR[row*aCols+colOut] = val;
		}
	}

	PrintComplex2D(&outR.front(), aRows, aCols);*/

	/*int N = sizeof(test) / sizeof(test[0]);

	std::vector<BFComplex> out;
	std::vector<BFComplex> invOut;			

	BFComplex _a = BFComplex::Polar(1.0, 0.5f) * BFComplex::Polar(1.0, 2.01f);

	for (int k = 0; k < N; k++)
	{
		BFComplex val;		
		for (int n = 0; n < N; n++)		
			val += test[n] * BFComplex::Polar(1.0, - 2 * BF_PI_D * k * n / (double) N);		
		out.push_back(val);
	}

	for (int n = 0; n < N; n++)	
	{
		BFComplex val;		
		for (int k = 0; k < N; k++)		
			val += out[k] * BFComplex::Polar(1.0, 2 * BF_PI_D * k * n / (double) N);		
		invOut.push_back(BFComplex(1.0/N) * val);
	}*/

	
}

void FFTShift1D(BFComplex* array, int size, int pitch, int dir)
{
	BFComplex* prev = new BFComplex[size];
	for (int i = 0; i < size; i++)
		prev[i] = array[i * pitch];
	
	while (dir < 0)
		dir += size;

	for (int i = 0; i < size; i++)
		array[i * pitch] = prev[((i + dir) % size)];

	delete prev;
}

static void FFTShift2D(BFComplex* data, int cols, int rows)
{
	for (int col = 0; col < cols; col++)
		FFTShift1D(data + col, rows, cols, cols / 2);
	for (int row = 0; row < rows; row++)
		FFTShift1D(data + row*cols, cols, 1, rows / 2);	
}

static void FFTConvert(ImageData* source)
{	
	int aCols = source->mWidth;
	int aRows = source->mHeight;
	int count = aCols * aRows;
	BFComplex* nums = new BFComplex[count];
	for (int i = 0; i < count; i++)
	{		
		PackedColor& color = *((PackedColor*) &source->mBits[i]);
		nums[i].mR = PackedColorGetGray()(color);
	}
	
	//FFT
	for (int col = 0; col < aCols; col++)
		FFD_1D(nums + col, aRows, aCols, -1);		
	for (int row = 0; row < aRows; row++)
		FFD_1D(nums + row*aCols, aCols, 1, -1);		
	
	//INV FFT
	/*for (int col = 0; col < aCols; col++)
		FFD_1D(nums + col, aRows, aCols, 1);	
	for (int row = 0; row < aRows; row++)
		FFD_1D(nums + row*aCols, aCols, 1, 1);*/
	
	FFTShift2D(nums, aCols, aRows);

	double* fFTLog = new double[count];
	double maxLog = 0;
	for (int i = 0; i < count; i++)
	{
		double aLog = log(1 + nums[i].Magnitude());
		maxLog = std::max(maxLog, aLog);
		fFTLog[i] = aLog;
	}

	for (int i = 0; i < count; i++)
	{
		//int val = (int) nums[i].Magnitude() / 1;			
		int val = (int) (fFTLog[i] * 255 / maxLog); 
		val = std::min(255, std::max(0, val));

		source->mBits[i] = 0xFF000000 | val | (val << 8) | (val << 16);
	}

	delete fFTLog;
	delete [] nums;
}

void DCT_1D(double* array, int size, int pitch)
{
	double* prev = new double[size];
	for (int i = 0; i < size; i++)
		prev[i] = array[i*pitch];
	
	for (int idxOut = 0; idxOut < size; idxOut++)
	{		
		double val = 0;		
		double scale = (idxOut == 0) ? 1/sqrt(2.0) : 1;
		
		for (int idxIn = 0; idxIn < size; idxIn++)		
			val += prev[idxIn] * cos(idxOut * BF_PI * (2*idxIn + 1)/(2 * size));
		array[idxOut*pitch] = 0.5 * scale * val;
	}

	delete prev;
}

void IDCT_1D(double* array, int size, int pitch)
{
	double* prev = new double[size];
	for (int i = 0; i < size; i++)
		prev[i] = array[i*pitch];
	
	for (int idxOut = 0; idxOut < size; idxOut++)
	{		
		double val = 0;						
		for (int idxIn = 0; idxIn < size; idxIn++)		
		{
			double scale = (idxIn == 0) ? 1/sqrt(2.0) : 1;
			val += scale * prev[idxIn] * cos(idxIn * BF_PI * (2*idxOut + 1)/(2 * size));
		}
		array[idxOut*pitch] = 0.5 * val;
	}

	delete prev;
}

const int DCT_1D_I_TABLE[64] = 
{
	4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 
	4017, 3405, 2275, 799, -799, -2275, -3405, -4017, 
	3784, 1567, -1567, -3784, -3784, -1567, 1567, 3784, 
	3405, -799, -4017, -2275, 2275, 4017, 799, -3405, 
	2896, -2896, -2896, 2896, 2896, -2896, -2896, 2896, 
	2275, -4017, 799, 3405, -3405, -799, 4017, -2275, 
	1567, -3784, 3784, -1567, -1567, 3784, -3784, 1567, 
	799, -2275, 3405, -4017, 4017, -3405, 2275, -799
};

void DCT_1D_I(int* array, int size, int pitch)
{
	int* prev = new int[size];
	for (int i = 0; i < size; i++)
		prev[i] = array[i*pitch];
		
	int multIdx = 0;
	for (int idxOut = 0; idxOut < size; idxOut++)
	{		
		int val = 0;		
	
		for (int idxIn = 0; idxIn < size; idxIn++)
			val += prev[idxIn] * DCT_1D_I_TABLE[multIdx++];
		
		if (idxOut == 0)		
			array[idxOut*pitch] = val / 0x2d41;
		else		
			array[idxOut*pitch] = val / 0x2000;

	}

	delete prev;
}

const int IDCT_1D_I_TABLE[64] = 
{
	2896, 4017, 3784, 3405, 2896, 2275, 1567, 799, 
	2896, 3405, 1567, -799, -2896, -4017, -3784, -2275, 
	2896, 2275, -1567, -4017, -2896, 799, 3784, 3405, 
	2896, 799, -3784, -2275, 2896, 3405, -1567, -4017, 
	2896, -799, -3784, 2275, 2896, -3405, -1567, 4017, 
	2896, -2275, -1567, 4017, -2896, -799, 3784, -3405, 
	2896, -3405, 1567, 799, -2896, 4017, -3784, 2275, 
	2896, -4017, 3784, -3405, 2896, -2275, 1567, -799
};

void IDCT_1D_I(int* array, int size, int pitch)
{
	int prev[64];
	for (int i = 0; i < size; i++)
		prev[i] = array[i*pitch];
		
	int multIdx = 0;
	for (int idxOut = 0; idxOut < size; idxOut++)
	{		
		int val = 0;				
		for (int idxIn = 0; idxIn < size; idxIn++)
			val += prev[idxIn] * IDCT_1D_I_TABLE[multIdx++];		
		array[idxOut*pitch] = val / 0x2000;
	}
}

void BFIData::Compress(ImageData* source)
{
	/*DFTTest();
	DFTTest_2D();
	DFTTest_Mine();*/

	//FFTConvert(source);

	/*const int rowCount = 3;
	const int colCount = 4;

	double mat[rowCount][colCount] = {{2, 2, 5, 5}, {4, 4, 10, 11}, {3, 2, 6, 4}};
	double rightVals[rowCount] = {1, 2, 3};
	int pivotIdx[colCount] = {-1, -1, -1, -1};

	int moveIdx = 0;
	
	
	double pivotVals[colCount] = {0};

	for (int pivot = 0; pivot < colCount; pivot++)
	{	
		bool moved = false;

		for (int eq = moveIdx; eq < rowCount; eq++)
		{			
			if (!IS_ZERO(mat[eq][pivot]))
			{				
				pivotVals[pivot] = mat[eq][pivot];
				for (int swapIdx = 0; swapIdx < colCount; swapIdx++)
					std::swap(mat[eq][swapIdx], mat[moveIdx][swapIdx]);
				std::swap(rightVals[eq], rightVals[moveIdx]);
				pivotIdx[pivot] = moveIdx;
				moved = true;
				break;
			}
		}

		if (!moved)
			continue;

		for (int eq = moveIdx + 1; eq < rowCount; eq++)
		{
			double multFactor = -mat[eq][pivot] / mat[moveIdx][pivot];			
			bool nonZero = false;
			for (int multIdx = pivot; multIdx < colCount; multIdx++)						
			{
				mat[eq][multIdx] += mat[moveIdx][multIdx]*multFactor;			
				nonZero |= !IS_ZERO(mat[eq][multIdx]);
			}
			rightVals[eq] += rightVals[moveIdx]*multFactor;
			BF_ASSERT(IS_ZERO(rightVals[eq]) || (nonZero));
		}

		moveIdx++;
	}

	// Back substitute
	double result[colCount];
	for (int col = colCount - 1; col >= 0; col--)
	{
		result[col] = 0;
		if (pivotIdx[col] != -1)
		{
			int eq = pivotIdx[col];
			double left = 0;			
			for (int multCol = col + 1; multCol < colCount; multCol++)
				left += mat[eq][multCol] * result[multCol];
			double right = rightVals[eq] - left;
			right /= mat[eq][col];
			result[col] = right;
		}		
	}

	///*/


	uint8 rawData[64] = {
		52, 55, 61, 66, 70, 61, 64, 73, 
		63, 59, 55, 90,109, 85, 69, 72, 
		62, 59, 68,113,144,104, 66, 73, 
		63, 58, 71,122,154,106, 70, 69, 
		67, 61, 68,104,126, 88, 68, 70,
		79, 65, 60, 70, 77, 68, 58, 75, 
		85, 71, 64, 59, 55, 61, 65, 83,
		87, 79, 69, 68, 65, 76, 78, 94};

	double dCTIn[64];
	for (int i = 0; i < 64; i++)	
		dCTIn[i] = (int) rawData[i] - 128;
	
	//double dCTOut[64];

	/*for (int v = 0; v < 8; v++)
	{
		for (int u = 0; u < 8; u++)
		{
			dCTOut[u+v*8] = 0;

			for (int y = 0; y < 8; y++)
			{
				for (int x = 0; x < 8; x++)
				{
					double au = (u == 0) ? sqrt(1.0/8.0) : sqrt(2.0/8.0);
					double av = (v == 0) ? sqrt(1.0/8.0) : sqrt(2.0/8.0);
					dCTOut[u+v*8] += au*av*dCTIn[x+y*8]*
						cos(BF_PI_D/8 * (x + 0.5)*u) *
						cos(BF_PI_D/8 * (y + 0.5)*v);
				}
			}
		}
	}*/

	double dCT[64];
	for (int i = 0; i < 64; i++)		
		dCT[i] = rawData[i] - 128;		

	int aCols = 8;
	int aRows = 8;

	for (int col = 0; col < aCols; col++)
		DCT_1D(dCT + col, aRows, aCols);	
	for (int row = 0; row < aRows; row++)
		DCT_1D(dCT + row*aCols, aCols, 1);	

	for (int col = 0; col < aCols; col++)
		IDCT_1D(dCT + col, aRows, aCols);	
	for (int row = 0; row < aRows; row++)
		IDCT_1D(dCT + row*aCols, aCols, 1);

	//

	int dCT_I[64];
	for (int i = 0; i < 64; i++)	
		dCT_I[i] = (rawData[i] - 128) * 256;	

	for (int col = 0; col < aCols; col++)
		DCT_1D_I(dCT_I + col, aRows, aCols);	
	for (int row = 0; row < aRows; row++)
		DCT_1D_I(dCT_I + row*aCols, aCols, 1);	

	for (int col = 0; col < aCols; col++)
		IDCT_1D_I(dCT_I + col, aRows, aCols);	
	for (int row = 0; row < aRows; row++)
		IDCT_1D_I(dCT_I + row*aCols, aCols, 1);	

	for (int i = 0; i < 64; i++)	
	{
		int val = dCT_I[i];
		if (val < 0)
			dCT_I[i] = (val - 128) / 256;	
		else
			dCT_I[i] = (val + 128) / 256;	
	}

	///

	int quantTable[] = {16, 11, 11, 16, 23, 27, 31, 30, 11, 12, 12, 15, 20, 23, 23, 30,
				11, 12, 13, 16, 23, 26, 35, 47, 16, 15, 16, 23, 26, 37, 47, 64,
				23, 20, 23, 26, 39, 51, 64, 64, 27, 23, 26, 37, 51, 64, 64, 64,
				31, 23, 35, 47, 64, 64, 64, 64, 30, 30, 47, 64, 64, 64, 64, 64};

	int quantDCT[64];
	for (int i = 0; i < 64; i++)
		quantDCT[i] = (int) BFRound((float) (dCT[i] / quantTable[i]));

	int zigZag[64] = {
	   0,  1,  5,  6, 14, 15, 27, 28,
	   2,  4,  7, 13, 16, 26, 29, 42,
	   3,  8, 12, 17, 25, 30, 41, 43,
	   9, 11, 18, 24, 31, 40, 44, 53,
	  10, 19, 23, 32, 39, 45, 52, 54,
	  20, 22, 33, 38, 46, 51, 55, 60,
	  21, 34, 37, 47, 50, 56, 59, 61,
	  35, 36, 48, 49, 57, 58, 62, 63
	};

	int zigZagDCT[64];
	for (int i = 0; i < 64; i++)
	{
		zigZagDCT[i] = quantDCT[zigZag[i]];
	}

	

	/*double dCTOutH[64];
	double dCTOutV[64];

	for (int v = 0; v < 8; v++)
	{
		for (int u = 0; u < 8; u++)
		{	
			dCTOutV[u+v*8] = 0;
			for (int y = 0; y < 8; y++)
			{
				double av = (v == 0) ? sqrt(1.0/8.0) : sqrt(2.0/8.0);				
				dCTOutV[u+v*8] += av*dCTIn[u+y*8]*						
						cos(BF_PI_D/8 * (y + 0.5)*v);
			}
		}
	}

	for (int v = 0; v < 8; v++)
	{
		for (int u = 0; u < 8; u++)
		{	
			dCTOutH[u+v*8] = 0;
			for (int x = 0; x < 8; x++)
			{				
				double au = (u == 0) ? sqrt(1.0/8.0) : sqrt(2.0/8.0);
				dCTOutH[u+v*8] += au*dCTOutV[x+v*8]*
					cos(BF_PI_D/8 * (x + 0.5)*u);
			}

			//dCTOut[u+v*8] = val;
		}
	}*/

	
}
