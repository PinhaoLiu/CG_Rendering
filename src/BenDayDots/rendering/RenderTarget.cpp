#include "rendering/RenderTarget.hpp"

#include "core/Log.h"

namespace benday
{
	RenderTarget::~RenderTarget()
	{
		shutdown();
	}

	bool RenderTarget::resize(int const width, int const height)
	{
		if (width <= 0 || height <= 0)
			return false;
		if (mFramebuffer != 0u && width == mWidth && height == mHeight)
			return true;

		shutdown();
		mWidth = width;
		mHeight = height;

		glGenTextures(1, &mColorTexture);
		glBindTexture(GL_TEXTURE_2D, mColorTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mWidth, mHeight, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glGenTextures(1, &mNormalTexture);
		glBindTexture(GL_TEXTURE_2D, mNormalTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mWidth, mHeight, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glGenTextures(1, &mDepthTexture);
		glBindTexture(GL_TEXTURE_2D, mDepthTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, mWidth, mHeight, 0,
		             GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glGenFramebuffers(1, &mFramebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		                       GL_TEXTURE_2D, mColorTexture, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
		                       GL_TEXTURE_2D, mNormalTexture, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		                       GL_TEXTURE_2D, mDepthTexture, 0);
		GLenum const draw_buffers[] = {
			GL_COLOR_ATTACHMENT0,
			GL_COLOR_ATTACHMENT1
		};
		glDrawBuffers(2, draw_buffers);

		bool const complete =
			glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
		glBindFramebuffer(GL_FRAMEBUFFER, 0u);
		glBindTexture(GL_TEXTURE_2D, 0u);

		if (!complete) {
			LogError("Ben-Day scene framebuffer is incomplete at %d x %d.", width, height);
			shutdown();
			return false;
		}
		return true;
	}

	void RenderTarget::shutdown()
	{
		if (mFramebuffer != 0u)
			glDeleteFramebuffers(1, &mFramebuffer);
		if (mColorTexture != 0u)
			glDeleteTextures(1, &mColorTexture);
		if (mDepthTexture != 0u)
			glDeleteTextures(1, &mDepthTexture);
		if (mNormalTexture != 0u)
			glDeleteTextures(1, &mNormalTexture);

		mFramebuffer = 0u;
		mColorTexture = 0u;
		mDepthTexture = 0u;
		mNormalTexture = 0u;
		mWidth = 0;
		mHeight = 0;
	}

	void RenderTarget::bindForWriting(bool const write_normals) const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
		GLenum const draw_buffers[] = {
			GL_COLOR_ATTACHMENT0,
			write_normals ? GL_COLOR_ATTACHMENT1 : GL_NONE
		};
		glDrawBuffers(2, draw_buffers);
	}
}
