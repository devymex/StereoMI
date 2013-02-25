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

//BOOL CopyBlock(BYTE *pImg, SIZE imgDimension, RECT blockRect, BYTE *pBuf)
//{
//	if (blockRect.left < 0 || blockRect.right >= imgDimension.cx - 1 ||
//		blockRect.top < 0 || blockRect.bottom >= imgDimension.cy - 1)
//	{
//		return FALSE;
//	}
//	UINT nBlockWidth = blockRect.right - blockRect.left + 1;
//	for (int r = blockRect.top; r <= blockRect.bottom; ++r)
//	{
//		UINT nImgIdx = r * imgDimension.cx + blockRect.left;
//		UINT nBufIdx = (r - blockRect.top) * nBlockWidth;
//		CopyMemory(pBuf + nBufIdx, pImg + nImgIdx, nBlockWidth);
//	}
//	return TRUE;
//}

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

BOOL StereoMI(BYTE *pImg1, const RECT &blk1, BYTE *pImg2, const RECT &blk2,
			  SIZE dim, BYTE byBins, float &fMI)
{
	SIZE blkSize = {blk1.right - blk1.left + 1, blk1.bottom - blk1.top + 1};
	if (blkSize.cx != blk2.right - blk2.left + 1 || blkSize.cy != blk2.bottom - blk2.top + 1)
	{
		return FALSE;
	}
	// Find the Min and Max value in two pictures;
	BYTE byMax1 = pImg1[0], byMin1 = pImg1[0], byMax2 = pImg2[0], byMin2 = pImg2[0];
	for (UINT r = 0; r != blkSize.cy; ++r)
	{
		for (UINT c = 0; c != blkSize.cx; ++c)
		{
			UINT nIdx1 = (r + blk1.top) * dim.cx + c + blk1.left;
			UINT nIdx2 = (r + blk2.top) * dim.cx + c + blk2.left;
			if (pImg1[nIdx1] < byMin1)
			{
				byMin1 = pImg1[nIdx1];
			}
			else if (pImg1[nIdx1] > byMax1)
			{
				byMax1 = pImg1[nIdx1];
			}
			if (pImg2[nIdx2] < byMin2)
			{
				byMin2 = pImg2[nIdx2];
			}
			else if (pImg2[nIdx2] > byMax2)
			{
				byMax2 = pImg2[nIdx2];
			}
		}
	}

	// Normalize value every pixel to [0, 1]
	UINT nBlkSize = blkSize.cx * blkSize.cy;
	float *pTemp1 = (float*)VirtualAlloc(NULL, nBlkSize * sizeof(float), MEM_COMMIT, PAGE_READWRITE);
	float *pTemp2 = (float*)VirtualAlloc(NULL, nBlkSize * sizeof(float), MEM_COMMIT, PAGE_READWRITE);

	for (UINT r = 0; r != blkSize.cy; ++r)
	{
		for (UINT c = 0; c != blkSize.cx; ++c)
		{
			UINT nIdx1 = (r + blk1.top) * dim.cx + c + blk1.left;
			UINT nIdx2 = (r + blk2.top) * dim.cx + c + blk2.left;

			pTemp1[nIdx1] = float(pImg1[nIdx1] - byMin1) / float(byMax1 - byMin1);
			pTemp2[nIdx2] = float(pImg2[nIdx2] - byMin2) / float(byMax2 - byMin2);
		}
	}

	// Prepare memory for histograph
	const BYTE byBins = 20;
	UINT Histo1[256] = {0}, Histo2[256] = {0}, HistoJ[256 * 256] = {0};

	// Calculate the Histograph
	for (UINT r = 0; r != blkSize.cy; ++r)
	{
		for (UINT c = 0; c != blkSize.cx; ++c)
		{
			UINT nIdx = r * blkSize.cx + c;

			BYTE byPix1 = BYTE(pTemp1[nIdx] * (byBins - 1) + 0.5f);
			BYTE byPix2 = BYTE(pTemp2[nIdx] * (byBins - 1) + 0.5f);

			++Histo1[byPix1], ++Histo2[byPix2];
			++HistoJ[byPix1 * byBins + byPix2];
		}
	}

	VirtualFree(pTemp1, 0, MEM_RELEASE);
	VirtualFree(pTemp2, 0, MEM_RELEASE);

	// Calculate the Mutual Information
	float fImageSize = float(nBlkSize), fH1 = 0, fH2 = 0, fHJ = 0;

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

	return TRUE;
}

BOOL PsoFind(BYTE *pImg1, BYTE *pImg2, SIZE dim, POINT leftPoint, POINT &rightPoint)
{
	UINT nHalfBlkSize = 20;
	UINT nBlkSize = nHalfBlkSize * 2 + 1;

	RECT rcLeftBlk;
	rcLeftBlk.left = leftPoint.x - nHalfBlkSize;
	rcLeftBlk.right = leftPoint.x + nHalfBlkSize;
	rcLeftBlk.top = leftPoint.y - nHalfBlkSize;
	rcLeftBlk.bottom = leftPoint.y + nHalfBlkSize;

	struct PARTICLE
	{
		POINT coord;
		POINT best;
		float fBestVal;
	};

	std::vector<PARTICLE> swarm;
	for (UINT i = 0; i < 100; ++i)
	{
		PARTICLE particle;
		particle.coord.x = rand() % dim.cx;
		particle.coord.y = rand() % dim.cy;
		particle.best = particle.coord;
		particle.fBestVal = 0;
	}

	for (std::vector<PARTICLE>::iterator i = swarm.begin(); i != swarm.end(); ++i)
	{
		RECT rcRightBlk;
		rcRightBlk.left = i->coord.x - nHalfBlkSize;
		rcRightBlk.right = i->coord.x + nHalfBlkSize;
		rcRightBlk.top = i->coord.y - nHalfBlkSize;
		rcRightBlk.bottom = i->coord.y + nHalfBlkSize;
		float fMI;
		if (StereoMI(pImg1, rcLeftBlk, pImg2, rcRightBlk, dim, 20, fMI))
		{
		}
	}
}

//
//HRESULT Match(LPCWSTR pFn1, LPCWSTR pFn2, float &fMI)
//{
//	struct DTOR
//	{
//		~DTOR()
//		{
//			if (pLock1 != NULL)
//			{
//				pLock1->Release();
//			}
//			if (pLock2 != NULL)
//			{
//				pLock2->Release();
//			}
//			if (pBmp1 != NULL)
//			{
//				pBmp1->Release();
//			}
//			if (pBmp2 != NULL)
//			{
//				pBmp2->Release();
//			}
//			if (pFactory != NULL)
//			{
//				pFactory->Release();
//			}
//		}
//		IWICImagingFactory *pFactory;
//		IWICBitmap *pBmp1;
//		IWICBitmap *pBmp2;
//		IWICBitmapLock *pLock1;
//		IWICBitmapLock *pLock2;
//	} dtor = {0};
//
//	HVERIFY(CoInitialize(NULL));
//
//	// Create WIC factory
//	HVERIFY(CoCreateInstance(
//		CLSID_WICImagingFactory1,
//		NULL,
//		CLSCTX_INPROC_SERVER,
//		IID_PPV_ARGS(&dtor.pFactory)
//		));
//
//	HVERIFY(LoadBitmapGray(dtor.pFactory, pFn1, &dtor.pBmp1));
//
//	HVERIFY(LoadBitmapGray(dtor.pFactory, pFn2, &dtor.pBmp2));
//
//	HVERIFY(dtor.pBmp1->Lock(NULL, WICBitmapLockRead | WICBitmapLockWrite, &dtor.pLock1));
//	HVERIFY(dtor.pBmp2->Lock(NULL, WICBitmapLockRead | WICBitmapLockWrite, &dtor.pLock2));
//
//	UINT nWidth, nHeight, nWidth1, nHeight1;
//	HVERIFY(dtor.pLock1->GetSize(&nWidth, &nHeight));
//	HVERIFY(dtor.pLock2->GetSize(&nWidth1, &nHeight1));
//
//	if (nWidth != nWidth1 || nHeight != nHeight1)
//	{
//		return S_FALSE;
//	}
//
//	UINT nBufSize;
//	BYTE *pImg1, *pImg2;
//	HVERIFY(dtor.pLock1->GetDataPointer(&nBufSize, &pImg1));
//	HVERIFY(dtor.pLock2->GetDataPointer(&nBufSize, &pImg2));
//	
//	UINT nStride = (nWidth / 4 + (nWidth % 4 != 0)) * 4;
//
//	UINT nImageSize = nWidth * nHeight;
//	BYTE *pTemp1 = (BYTE*)VirtualAlloc(NULL, nImageSize, MEM_COMMIT, PAGE_READWRITE);
//	BYTE *pTemp2 = (BYTE*)VirtualAlloc(NULL, nImageSize, MEM_COMMIT, PAGE_READWRITE);
//
//	for (UINT r = 0; r != nHeight; ++r)
//	{
//		CopyMemory(pTemp1 + r * nWidth, pImg1 + r * nStride, nWidth);
//		CopyMemory(pTemp2 + r * nWidth, pImg2 + r * nStride, nWidth);
//	}
//
//	SIZE size = {nWidth, nHeight};
//	fMI = MI(pTemp1, pTemp2, size, 20);
//
//	VirtualFree(pTemp1, 0, MEM_RELEASE);
//	VirtualFree(pTemp2, 0, MEM_RELEASE);
//
//	return S_OK;
//}
//

int _tmain(int nArgC, _TCHAR* ppArgv[])
{
	if (nArgC < 3)
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

