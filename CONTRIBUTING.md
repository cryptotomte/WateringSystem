# Contributing Guidelines

Thank you for considering contributing to this project! Here's how you can help.

## Code of Conduct
This project adheres to a Code of Conduct. By participating, you are expected to uphold this code.

## License and Copyright

This project is licensed under the GNU Affero General Public License v3.0 or later (AGPL-3.0-or-later). By contributing to this project, you agree that your contributions will be licensed under the same license.

### Developer Certificate of Origin (DCO)

All contributions must include a "Signed-off-by" line in the commit message to certify that you have the right to submit your contribution under the project's license. This is done using the Developer Certificate of Origin (DCO).

To sign off on a commit, add the `-s` flag when committing:
```bash
git commit -s -m "Your commit message"
```

This adds a line like: `Signed-off-by: Your Name <your.email@example.com>`

### SPDX Headers

All new source files (.cpp, .h) must include SPDX headers at the top:
```cpp
// SPDX-FileCopyrightText: 2025 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
```

## How Can I Contribute?

### Reporting Bugs
- Use the GitHub issue tracker
- Describe the bug with clear steps to reproduce
- Include screenshots if applicable
- Include your environment details

### Suggesting Features
- Use the GitHub issue tracker with the "enhancement" label
- Clearly describe the feature and its benefits
- Provide examples of how it would be used

### Code Contributions
1. Fork the repository
2. Create a feature branch: `git checkout -b feature/your-feature-name`
3. Make your changes
4. Add SPDX headers to any new files
5. Run tests to ensure they pass
6. Commit your changes with descriptive commit messages using `-s` flag
7. Push to your branch: `git push origin feature/your-feature-name`
8. Create a pull request

## Pull Request Process
1. Ensure your code follows the project's style guide
2. All new files must have SPDX headers
3. All commits must be signed off with DCO
4. Update documentation as needed
5. Write or update tests as needed
6. Make sure all tests pass
7. Get at least one code review

## Style Guides

### Git Commit Messages
- Use the present tense ("Add feature" not "Added feature")
- Use the imperative mood ("Move cursor to..." not "Moves cursor to...")
- Limit the first line to 72 characters or less
- Reference issues and pull requests after the first line
- Always include DCO sign-off: `git commit -s`

### Code Style
- Follow the existing code style of the project
- Comment your code where necessary (in English)
- Write clean, maintainable, and testable code
- Use strict C++ standards as defined in platformio.ini

### File Headers
- All C/C++ source files must start with SPDX headers
- Follow the existing pattern for Doxygen documentation comments

## Documentation
- Update the README.md with details of changes if appropriate
- Update the documentation when adding or changing features
- All code comments must be in English
- Use GitHub-flavored markdown for documentation

## Hardware Contributions
- Follow the hardware specifications in `docs/hardware.md`
- Update KiCad schemas and PCB designs as needed
- Include proper version control for hardware design files

## Copyleft Compliance

This project uses AGPL-3.0-or-later, which means:
- All modifications must be shared under the same license
- If you distribute binaries, you must also provide source code
- If you run modified versions on a server, you must provide source access to users
- You must preserve copyright notices and license information

Thank you for contributing!
