#include "CabUtil.h"

#ifdef BF_PLATFORM_WINDOWS

#pragma warning(disable:4996)

#pragma comment(lib, "cabinet.lib")

#include <FDI.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

USING_NS_BF;

static char* FDIErrorToString(FDIERROR err)
{
	switch (err)
	{
	case FDIERROR_NONE:
		return "No error";
	case FDIERROR_CABINET_NOT_FOUND:
		return "Cabinet not found";
	case FDIERROR_NOT_A_CABINET:
		return "Not a cabinet";
	case FDIERROR_UNKNOWN_CABINET_VERSION:
		return "Unknown cabinet version";
	case FDIERROR_CORRUPT_CABINET:
		return "Corrupt cabinet";
	case FDIERROR_ALLOC_FAIL:
		return "Memory allocation failed";
	case FDIERROR_BAD_COMPR_TYPE:
		return "Unknown compression type";
	case FDIERROR_MDI_FAIL:
		return "Failure decompressing data";
	case FDIERROR_TARGET_FILE:
		return "Failure writing to target file";
	case FDIERROR_RESERVE_MISMATCH:
		return "Cabinets in set have different RESERVE sizes";
	case FDIERROR_WRONG_CABINET:
		return "Cabinet returned on fdintNEXT_CABINET is incorrect";
	case FDIERROR_USER_ABORT:
		return "Application aborted";
	default:
		return "Unknown error";
	}
}

CabFile::CabFile()
{
	
}

static FNALLOC(CabMemAlloc)
{
	return malloc(cb);
}

static FNFREE(CabMemFree)
{
	free(pv);
}

static FNOPEN(CabFileOpen)
{
	return _open(pszFile, oflag, pmode);
}

static FNREAD(CabFileRead)
{
	return _read((int)hf, pv, cb);
}

static FNWRITE(CabFileWrite)
{
	return _write((int)hf, pv, cb);
}

static FNCLOSE(CabFileClose)
{
	return _close((int)hf);
}

static FNSEEK(CabFileSeek)
{
	return _lseek((int)hf, dist, seektype);
}

static FNFDINOTIFY(CabFDINotify)
{	
	CabFile* cabFile = (CabFile*)pfdin->pv;

	switch (fdint)
	{
	case fdintCOPY_FILE:
		{
			String destName = cabFile->mDestDir;
			destName += "/";
			destName += pfdin->psz1;
			int fh = (int)CabFileOpen((LPSTR)destName.c_str(), _O_BINARY | _O_CREAT | _O_WRONLY | _O_SEQUENTIAL,
				_S_IREAD | _S_IWRITE);
			if (fh == -1)
				cabFile->Fail(StrFormat("Failed to create '%s'", destName.c_str()));
			return fh;
		}
	case fdintCLOSE_FILE_INFO:
		CabFileClose(pfdin->hf);
		return TRUE;
	default:
		break;
	}
	return 0;
}

void CabFile::Fail(const StringImpl& err)
{
	if (mError.IsEmpty())
		mError = err;
}

bool CabFile::Load(const StringImpl& path)
{
	if (!FileExists(path))
	{
		Fail(StrFormat("File '%s' not found", path.c_str()));
		return false;
	}
	mSrcFileName = path;
	return true;
}

bool CabFile::DecompressAll(const StringImpl& destDir)
{
	if (mSrcFileName.IsEmpty())
	{
		Fail("No file specified");
		return false;
	}

	ERF erf = { 0 };
	HFDI fdi = FDICreate(CabMemAlloc, CabMemFree, CabFileOpen, CabFileRead, CabFileWrite, CabFileClose, CabFileSeek, cpu80386, &erf);
	if (!fdi)
		return false;

	RecursiveCreateDirectory(destDir);
	mDestDir = destDir;
	bool result = FDICopy(fdi, (LPSTR)GetFileName(mSrcFileName).c_str(), (LPSTR)(GetFileDir(mSrcFileName) + "/").c_str(), 0, CabFDINotify, NULL, this) != 0;

	if (!result)
		Fail(FDIErrorToString((FDIERROR)erf.erfOper));

	FDIDestroy(fdi);

	return result;
}

#endif
