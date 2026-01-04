# GitHub Actions CI/CD Detailed Guide

This guide aims to help new developers understand and use the automated build, test, and release processes in the TaskEngine project. Through GitHub Actions, we can ensure code stability across different operating systems and achieve automated version releases.

## 1. What is CI/CD?

*   **CI (Continuous Integration)**: Whenever you submit code, the system automatically runs builds and tests in the cloud to ensure your changes haven't broken existing functionality.
*   **CD (Continuous Deployment/Delivery)**: When you are ready to release a new version, the system automatically packages binary files and creates a GitHub Release.

## 2. Workflows in the Project

This project defines two main workflows, stored in the `.github/workflows` directory:

### 2.1 Vcpkg CI (`vcpkg-ci.yml`)
This is the core workflow responsible for cross-platform verification.
*   **Trigger Timing**: Pushes to the `main` branch, Pull Request submissions, or pushing version tags (`v*`).
*   **Core Functions**:
    *   **Matrix Build**: Runs simultaneously on Ubuntu (Linux), Windows, and macOS.
    *   **Dependency Management**: Uses `vcpkg` to automatically install dependencies (like GoogleTest).
    *   **Caching Mechanism**: Caches binary files compiled by `vcpkg`, reducing build time from 10 minutes to 2 minutes.
    *   **Automatic Release**: If triggered by a tag, it automatically uploads the compiled `.zip` or `.tar.gz` packages to GitHub Release.

### 2.2 Prepare Release (`prepare-release.yml`)
This is a manually triggered workflow used to simplify version number management.
*   **Trigger Timing**: Manually clicked to run on the GitHub web interface.
*   **Core Functions**: Automatically modifies version numbers in `CMakeLists.txt` and `vcpkg.json`, commits the code, and applies a Git tag.

---

## 3. Beginner's Operation Manual

### 3.1 How to View Build Status
1.  At the top of the GitHub repository page, click the **Actions** tab.
2.  The list on the left shows all workflows. Click **Vcpkg CI**.
3.  You can see the build records corresponding to each commit. A green checkmark indicates success, and a red cross indicates failure.
4.  Click on a specific record to view detailed compilation and test logs for Windows/Linux/macOS respectively.

### 3.2 How to Release a New Version (Recommended Steps)
To ensure version number consistency, please follow these steps to release a new version:

1.  Enter the **Actions** page and select **Prepare Release** on the left.
2.  Click the **Run workflow** dropdown button on the right.
3.  Enter the new version number (e.g., `1.0.5`) in the **New Version** input box.
4.  Click **Run workflow**.
5.  **Subsequent Automation Process**:
    *   The script will automatically modify files and push a tag (e.g., `v1.0.5`).
    *   The tag push will trigger the **Vcpkg CI** workflow.
    *   After **Vcpkg CI** runs successfully, you will find a new version automatically appearing on the repository's **Releases** page, containing source code and pre-compiled binary packages.

---

## 4. Required Repository Permissions

To allow automation scripts to commit code and create Releases, you need to check the repository's permission settings:

1.  Go to **Settings** -> **Actions** -> **General** of the repository.
2.  Scroll to the **Workflow permissions** section.
3.  Select **Read and write permissions**.
4.  Check **Allow GitHub Actions to create and approve pull requests**.
5.  Click **Save**.

## 5. Troubleshooting

*   **What if the build fails?**
    *   Click on the failed Job and check the output of the `Run Tests` step. `ctest` will tell you exactly which test case failed.
*   **vcpkg cache not working?**
    *   The cache is usually generated after the first successful build. If `vcpkg.json` changes, the cache will automatically invalidate and regenerate.
*   **Prepare Release reports "Permission Denied"?**
    *   Please check the permission settings in Section 4 to ensure Actions have permission to push code to the repository.

---
*Reference this project to implement your own automation process!*