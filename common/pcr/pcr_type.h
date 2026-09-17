#pragma once

#include "pcr_format.h"

#include <string>
#include <vector>

namespace pcr
{
	using std::string;
	using std::vector;

	using SessionId = uint32_t;

	struct Frame
	{
		uint32_t packetSize;
		uint32_t frameCount;
		uint64_t deltaUs;
		vector<Point> points;
		vector<Target> targets;
	};

	struct RecordFrame
	{
		Frame frame;
		uint64_t bytes = 0;
	};

	struct Bookmark
	{
		string description;
		uint64_t frameIndex;
	};

	struct RecordSession
	{
		SessionId id;
		uint64_t timestamp;
		uint64_t lengthUs;
		uint64_t bytes;
		string name;
		string description;
		string tag;
		vector<RecordFrame> frames;
		vector<Bookmark> bookmarks;

		int ownerWindowId = -1;
	};
}
