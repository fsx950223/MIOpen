#ifndef GUARD_MLOPEN_CONTEXT_HPP_
#define GUARD_MLOPEN_CONTEXT_HPP_

#include <mlopen.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <memory>

struct mlopenContextImpl;

struct mlopenContext {
	
	mlopenContext();
	mlopenContext(int numStreams, mlopenAcceleratorQueue_t *streams);
	~mlopenContext();

	mlopenAcceleratorQueue_t GetStream();

	// Deprecated
	int GetStream (mlopenAcceleratorQueue_t *stream, ...)
	{
		*stream = this->GetStream();
		return 0;
	}

	std::unique_ptr<mlopenContextImpl> impl;
	
};


#endif // GUARD_MLOPEN_CONTEXT_HPP_
