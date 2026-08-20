#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

namespace sph
{
	class FixedThreadPool
	{
	public:
		using RangeFunction = void (*)(void* context,
		                               std::size_t job_index,
		                               std::size_t begin,
		                               std::size_t end);

		explicit FixedThreadPool(std::size_t maximum_thread_count);
		~FixedThreadPool();

		FixedThreadPool(FixedThreadPool const&) = delete;
		FixedThreadPool& operator=(FixedThreadPool const&) = delete;

		void parallelFor(std::size_t item_count,
		                 std::size_t requested_thread_count,
		                 void* context,
		                 RangeFunction function);
		std::size_t maximumThreadCount() const noexcept;

	private:
		struct Job
		{
			void* context{ nullptr };
			RangeFunction function{ nullptr };
			std::size_t itemCount{ 0u };
			std::size_t activeThreadCount{ 1u };
		};

		static void executeRange(Job const& job, std::size_t job_index);
		void workerLoop(std::size_t worker_index);

		std::size_t mMaximumThreadCount;
		std::vector<std::thread> mWorkers;
		std::mutex mMutex;
		std::condition_variable mJobAvailable;
		std::condition_variable mJobComplete;
		Job mJob;
		std::size_t mGeneration{ 0u };
		std::size_t mCompletedWorkerCount{ 0u };
		bool mStopping{ false };
	};
}
