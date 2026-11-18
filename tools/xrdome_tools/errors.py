"""Exit codes and the one exception the command line turns into an exit status."""

EXIT_OK = 0
EXIT_EXISTS = 1
EXIT_SETUP = 2
EXIT_VERIFY = 3
EXIT_INTERRUPTED = 130


class ToolError(Exception):
    """A refusal or failure with a message for the user and an exit code."""

    def __init__(self, message: str, code: int = EXIT_SETUP):
        super().__init__(message)
        self.code = code
