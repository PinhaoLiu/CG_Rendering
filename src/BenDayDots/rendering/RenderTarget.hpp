#pragma once

#include <glad/glad.h>

namespace benday
{
	class RenderTarget
	{
	public:
		RenderTarget() = default;
		~RenderTarget();

		RenderTarget(RenderTarget const&) = delete;
		RenderTarget& operator=(RenderTarget const&) = delete;

		bool resize(int width, int height);
		void shutdown();
		void bindForWriting(bool write_normals) const;

		GLuint colorTexture() const { return mColorTexture; }
		GLuint depthTexture() const { return mDepthTexture; }
		GLuint normalTexture() const { return mNormalTexture; }

	private:
		GLuint mFramebuffer{ 0u };
		GLuint mColorTexture{ 0u };
		GLuint mDepthTexture{ 0u };
		GLuint mNormalTexture{ 0u };
		int mWidth{ 0 };
		int mHeight{ 0 };
	};
}
