/**
 ******************************************************************************
 * DBZ3 - version stamp shared by the runtime components.
 ******************************************************************************
 *
 * The runtime DLLs (rexruntime, rexgpu-xenos) carry no VERSIONINFO, so a folder
 * that mixes files from two releases cannot be identified from the outside: the
 * exe says 1.2.1 while the DLLs are from a newer build, and a bug report from
 * that install is impossible to reproduce (it happened with the logs that
 * motivated this check).
 *
 * Both components publish this string through a cvar of their own
 * (`dbz3_runtime_build` / `dbz3_gpu_build`), which the launcher reads to warn
 * about a mixed install, and print it in the environment line of the log.
 *
 * BUMP THIS TOGETHER WITH src/version.rc (tools/verify_release.ps1 checks that
 * the value in the built DLLs matches the release version).
 */

#pragma once

#define DBZ3_RUNTIME_BUILD "1.2.9"
