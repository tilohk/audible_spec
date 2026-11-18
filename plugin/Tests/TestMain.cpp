#include <juce_core/juce_core.h>

// Runs every juce::UnitTest registered in this executable.
// Exit code 0 = all passed (used by ctest).
int main (int argc, char* argv[])
{
    juce::ignoreUnused (argc, argv);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        failures += runner.getResult (i)->failures;
        passes += runner.getResult (i)->passes;
    }
    std::printf ("\n%d checks passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
