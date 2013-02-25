// StereoMI.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#pragma comment(lib, "Shlwapi.lib")
//#pragma comment(lib, "windowscodecs.lib")

#define HVERIFY(fn) {HRESULT hr = fn; if (FAILED(hr)) return hr;}

template<typename _Ty>
inline _Ty log2(_Ty val)
{
	return std::log(val) / _Ty(M_LN2);
}


HRESULT LoadBitmapGray(IWICImagingFactory *pFactory, LPCWSTR pFileName, IWICBitmap **ppResult)
{
	struct DTOR
	{
		~DTOR()
		{
			if (pConverter != NULL)
			{
				pConverter->Release();
			}
			if (pFrame != NULL)
			{
				pFrame->Release();
			}
			if (pDecoder != NULL)
			{
				pDecoder->Release();
			}
		}
		IWICBitmapDecoder *pDecoder;
		IWICBitmapFrameDecode *pFrame;
		IWICFormatConverter *pConverter;
	} dtor = {0};

	// Create decoder from given file
	HVERIFY(pFactory->CreateDecoderFromFilename(
		pFileName,							// Image to be decoded
		NULL,								// Do not prefer a particular vendor
		GENERIC_READ,						// Desired read access to the file
		WICDecodeMetadataCacheOnDemand,		// Cache metadata when needed
		&dtor.pDecoder						// Pointer to the decoder
		));

	UINT nFrameCount = 0;

	// Retrieve the frame count of the image.
	HVERIFY(dtor.pDecoder->GetFrameCount(&nFrameCount));

	if (nFrameCount == 0)
	{
		return S_FALSE;
	}

	// Retrieve the first bitmap frame.
	HVERIFY(dtor.pDecoder->GetFrame(0, &dtor.pFrame));

	// Create format converter
	HVERIFY(pFactory->CreateFormatConverter(&dtor.pConverter));

	HVERIFY(dtor.pConverter->Initialize(
		dtor.pFrame,						// Input source to convert
		GUID_WICPixelFormat8bppGray,		// Destination pixel format
		WICBitmapDitherTypeNone,			// Specified dither pattern
		NULL,								// Specify a particular palette 
		0.f,								// Alpha threshold
		WICBitmapPaletteTypeCustom			// Palette translation type
		));

	HVERIFY(pFactory->CreateBitmapFromSource(
		dtor.pConverter,					// Format converter interface
		WICBitmapCacheOnDemand,				// Create a system memory copy
		ppResult));							// Pointer to the WICBitmap interface

	return S_OK;
}


HRESULT MutualInformation(LPCWSTR pFn1, LPCWSTR pFn2, float &fMI)
{
	struct DTOR
	{
		~DTOR()
		{
			if (pLock1 != NULL)
			{
				pLock1->Release();
			}
			if (pLock2 != NULL)
			{
				pLock2->Release();
			}
			if (pBmp1 != NULL)
			{
				pBmp1->Release();
			}
			if (pBmp2 != NULL)
			{
				pBmp2->Release();
			}
			if (pFactory != NULL)
			{
				pFactory->Release();
			}
		}
		IWICImagingFactory *pFactory;
		IWICBitmap *pBmp1;
		IWICBitmap *pBmp2;
		IWICBitmapLock *pLock1;
		IWICBitmapLock *pLock2;
	} dtor = {0};

	HVERIFY(CoInitialize(NULL));

	// Create WIC factory
	HVERIFY(CoCreateInstance(
		CLSID_WICImagingFactory1,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&dtor.pFactory)
		));

	HVERIFY(LoadBitmapGray(dtor.pFactory, pFn1, &dtor.pBmp1));

	HVERIFY(LoadBitmapGray(dtor.pFactory, pFn2, &dtor.pBmp2));

	HVERIFY(dtor.pBmp1->Lock(NULL, WICBitmapLockRead | WICBitmapLockWrite, &dtor.pLock1));
	HVERIFY(dtor.pBmp2->Lock(NULL, WICBitmapLockRead | WICBitmapLockWrite, &dtor.pLock2));

	UINT nWidth, nHeight, nWidth1, nHeight1;
	HVERIFY(dtor.pLock1->GetSize(&nWidth, &nHeight));
	HVERIFY(dtor.pLock2->GetSize(&nWidth1, &nHeight1));

	if (nWidth != nWidth1 || nHeight != nHeight1)
	{
		return S_FALSE;
	}

	UINT nBufSize;
	BYTE *pBuf1, *pBuf2;
	HVERIFY(dtor.pLock1->GetDataPointer(&nBufSize, &pBuf1));
	HVERIFY(dtor.pLock2->GetDataPointer(&nBufSize, &pBuf2));
	
	UINT nStride = (nWidth / 4 + (nWidth % 4 != 0)) * 4;

	// Find the Min and Max value in two pictures;
	BYTE byMax1 = pBuf1[0], byMin1 = pBuf1[0], byMax2 = pBuf2[0], byMin2 = pBuf2[0];
	for (UINT r = 0; r != nHeight; ++r)
	{
		for (UINT c = 0; c != nWidth; ++c)
		{
			UINT nIdx = r * nStride + c;
			if (pBuf1[nIdx] < byMin1)
			{
				byMin1 = pBuf1[nIdx];
			}
			else if (pBuf1[nIdx] > byMax1)
			{
				byMax1 = pBuf1[nIdx];
			}
			if (pBuf2[nIdx] < byMin2)
			{
				byMin2 = pBuf2[nIdx];
			}
			else if (pBuf2[nIdx] > byMax2)
			{
				byMax2 = pBuf2[nIdx];
			}
		}
	}

	// Normalize value every pixel to [0, 1]
	UINT nImageSize = nWidth * nHeight;
	float *pTemp1 = (float*)VirtualAlloc(NULL, nImageSize * sizeof(float), MEM_COMMIT, PAGE_READWRITE);
	float *pTemp2 = (float*)VirtualAlloc(NULL, nImageSize * sizeof(float), MEM_COMMIT, PAGE_READWRITE);

	for (UINT r = 0; r != nHeight; ++r)
	{
		for (UINT c = 0; c != nWidth; ++c)
		{
			UINT nBufIdx = r * nStride + c;
			UINT nTempIdx = r * nWidth + c;

			pTemp1[nTempIdx] = float(pBuf1[nBufIdx] - byMin1) / float(byMax1 - byMin1);
			pTemp2[nTempIdx] = float(pBuf2[nBufIdx] - byMin2) / float(byMax2 - byMin2);
		}
	}

	// Prepare memory for histograph
	const BYTE byBins = 20;
	UINT Histo1[byBins] = {0}, Histo2[byBins] = {0}, HistoJ[byBins * byBins] = {0};

	// Calculate the Histograph
	for (UINT r = 0; r != nHeight; ++r)
	{
		for (UINT c = 0; c != nWidth; ++c)
		{
			UINT nIdx = r * nStride + c;
			UINT nTempIdx = r * nWidth + c;

			pBuf1[nIdx] = BYTE(pTemp1[nTempIdx] * (byBins - 1) + 0.5f);
			pBuf2[nIdx] = BYTE(pTemp2[nTempIdx] * (byBins - 1) + 0.5f);

			++Histo1[pBuf1[nIdx]], ++Histo2[pBuf2[nIdx]];
			++HistoJ[pBuf1[nIdx] * byBins + pBuf2[nIdx]];
		}
	}

	// Calculate the Mutual Information
	float fImageSize = float(nImageSize), fH1 = 0, fH2 = 0, fHJ = 0;

	for (UINT i = 0; i < byBins; ++i)
	{
		if (Histo1[i] != 0)
		{
			float fP1 = float(Histo1[i]) / fImageSize;
			fH1 += -fP1 * log2(fP1);
		}
		
		if (Histo2[i] != 0)
		{
			float fP2 = float(Histo2[i]) / fImageSize;
			fH2 += -fP2 * log2(fP2);
		}
	}

	for (UINT i = 0; i < byBins * byBins; ++i)
	{
		if (HistoJ[i] != 0)
		{
			float fPJ = float(HistoJ[i]) / fImageSize;
			fHJ += -fPJ * log2(fPJ);
		}
	}
	fMI = fH1 + fH2 - fHJ;
	return S_OK;
}


int _tmain(int nArgC, _TCHAR* ppArgv[])
{
	if (nArgC != 3)
	{
		std::cout << "Must input two filename of images." << std::endl;
		return -1;
	}

	if (!PathFileExistsW(ppArgv[1]) || !PathFileExistsW(ppArgv[2]))
	{
		std::cout << "At least one file not exsist." << std::endl;
		return -2;
	}

	float fMI;
	HRESULT hr = MutualInformation(ppArgv[1], ppArgv[2], fMI);
	if (S_OK == hr)
	{
		std::cout << fMI << std::endl;
	}
	else
	{
		std::cout << "Error: " << std::hex << hr << std::endl;
	}
	system("pause");
	return 0;
}

