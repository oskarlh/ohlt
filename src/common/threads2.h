#pragma once

#include <condition_variable>
#include <thread>

// Task:
// Each Task is diveded into 0-n Subtasks
// Each thread receives 1-n Subtasks (such as 10 models to process)
// Each Subtask consists of 1-n Units (such as 1 model to process)

// When the final result is new data:
// When a Subtask (a) finishes, the thread checks if a neighbouring (to the
// left or to the right) Subtask (b) has already finished, and merges the
// outputs.

// When the final result is modifications to existing data:
// Either a Subtask receives a span of mutable data, or we have the main
// thread do the job of applying all the data

struct job final { };

void thread_function(std::stop_token stopToken) {
	while (!stopToken.stop_requested()) {
		std::condition_variable();
	}
}

class threads final {
  private:
	std::vector<std::jthread> threads;

  public:
	void start_threads(std::size_t numThreads) {
		threads.clear();
		threads.reserve(numThreads);
		threads.emplace_back(fn);
		std::jthread e;
	}
};
