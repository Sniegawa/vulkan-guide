#pragma once

#include <functional>
#include <deque>



struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function)
	{
		deletors.push_back(function);
	}

	void flush()
	{
		// Reverse the queue
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
		{
			(*it)();
		}

		deletors.clear();
	}
};