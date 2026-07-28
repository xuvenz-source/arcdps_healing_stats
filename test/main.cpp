#include "../src/Log.h"

#ifdef _WIN32
#include "Exports.h"
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#else
#pragma warning(push, 0)
#pragma warning(disable : 4005)
#pragma warning(disable : 4389)
#pragma warning(disable : 26439)
#pragma warning(disable : 26495)
#endif
#include <gtest/gtest.h>
#ifdef __clang__
#pragma clang diagnostic pop
#else
#pragma warning(pop)
#endif

class TestLogFlusher : public testing::EmptyTestEventListener
{
	void OnTestStart(const ::testing::TestInfo& pTestInfo) override
	{
		LogI("Starting test {}.{}.{}", pTestInfo.test_suite_name(), pTestInfo.test_case_name(), pTestInfo.name());
	}

	// Called after a failed assertion or a SUCCESS().
	void OnTestPartResult(const testing::TestPartResult& /*pTestInfo*/) override
	{
		Log_::FlushLogFile();
	}

	// Called after a test ends.
	void OnTestEnd(const testing::TestInfo& /*pTestInfo*/) override
	{
		Log_::FlushLogFile();
	}
};

int main(int pArgumentCount, char** pArgumentVector)
{
#ifdef _WIN32
	GlobalObjects::IS_UNIT_TEST = true;
#endif

	Log_::Init(true, "logs/unit_tests.txt");
	Log_::SetLevel(spdlog::level::trace);
	Log_::LockLogger();

	::testing::InitGoogleTest(&pArgumentCount, pArgumentVector);

	testing::UnitTest::GetInstance()->listeners().Append(new TestLogFlusher);

	int result = RUN_ALL_TESTS();

	spdlog::shutdown();
	Log_::LOGGER = nullptr;

	return result;
}
