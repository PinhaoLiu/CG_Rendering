#include "simulation/FixedThreadPool.hpp"

#include <algorithm>
#include <stdexcept>

namespace sph
{
	FixedThreadPool::FixedThreadPool(std::size_t const maximum_thread_count) :
		mMaximumThreadCount(std::max<std::size_t>(1u, maximum_thread_count))
	{
		mWorkers.reserve(mMaximumThreadCount - 1u);
		for (std::size_t worker_index = 1u;
		     worker_index < mMaximumThreadCount;
		     ++worker_index)
			mWorkers.emplace_back(&FixedThreadPool::workerLoop, this, worker_index);
	}

	FixedThreadPool::~FixedThreadPool()
	{
		{
			std::lock_guard<std::mutex> const lock(mMutex);
			mStopping = true;
			++mGeneration;
		}
		mJobAvailable.notify_all();
		for (std::thread& worker : mWorkers)
			worker.join();
	}

	void FixedThreadPool::parallelFor(std::size_t const item_count,
	                                  std::size_t const requested_thread_count,
	                                  void* const context,
	                                  RangeFunction const function)
	{
		if (item_count == 0u)
			return;
		if (function == nullptr)
			throw std::invalid_argument("Parallel range function must not be null.");

		std::size_t const active_thread_count = std::max<std::size_t>(
			1u,
			std::min({ requested_thread_count, mMaximumThreadCount, item_count }));
		Job const job{ context, function, item_count, active_thread_count };
		if (active_thread_count == 1u) {
			executeRange(job, 0u);
			return;
		}

		{
			std::lock_guard<std::mutex> const lock(mMutex);
			mJob = job;
			mCompletedWorkerCount = 0u;
			++mGeneration;
		}
		mJobAvailable.notify_all();

		executeRange(job, 0u);

		std::unique_lock<std::mutex> lock(mMutex);
		mJobComplete.wait(lock, [&]() {
			return mCompletedWorkerCount == active_thread_count - 1u;
		});
	}

	std::size_t FixedThreadPool::maximumThreadCount() const noexcept
	{
		return mMaximumThreadCount;
	}

	void FixedThreadPool::executeRange(Job const& job,
	                                   std::size_t const job_index)
	{
		std::size_t const begin = job.itemCount * job_index / job.activeThreadCount;
		std::size_t const end = job.itemCount * (job_index + 1u) /
		                        job.activeThreadCount;
		job.function(job.context, job_index, begin, end);
	}

	void FixedThreadPool::workerLoop(std::size_t const worker_index)
	{
		std::size_t observed_generation = 0u;
		while (true) {
			Job job;
			{
				std::unique_lock<std::mutex> lock(mMutex);
				mJobAvailable.wait(lock, [&]() {
					return mStopping || mGeneration != observed_generation;
				});
				if (mStopping)
					return;
				observed_generation = mGeneration;
				job = mJob;
			}

			if (worker_index >= job.activeThreadCount)
				continue;
			executeRange(job, worker_index);

			{
				std::lock_guard<std::mutex> const lock(mMutex);
				++mCompletedWorkerCount;
				if (mCompletedWorkerCount == job.activeThreadCount - 1u)
					mJobComplete.notify_one();
			}
		}
	}
}
