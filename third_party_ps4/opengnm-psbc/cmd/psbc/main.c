/*
 * opengnm-psbc — SPIR-V to PS4/PS5 Shader Binary Compiler (CLI)
 *
 * Thin CLI wrapper around libpsbc. The compilation logic lives in
 * libpsbc/psbc_compile.c; this file handles argument parsing, file I/O,
 * and calling psbc_compile_shader().
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "psbc_compile.h"

static const char* VERSION_STR = "0.1.0";

typedef struct {
	const char* inputfile;
	const char* outputfile;
	const char* entrypoint;
	PsbcStage stage;
	bool showhelp;
	PsbcTarget target;
	bool optimise;
	bool verbose;
} CmdOptions;

static CmdOptions parsecmdoptions(int argc, char* argv[]) {
	CmdOptions res = {
	    .entrypoint = "main",
	    .stage = PSBC_STAGE_NONE,
	    .optimise = true,
	    .target = PSBC_TARGET_PS5,
	};

	for (int i = 0; i < argc; i += 1) {
		const char* curarg = argv[i];
		if (!strcmp(curarg, "-h")) {
			res.showhelp = true;
		} else if (!strcmp(curarg, "-f")) {
			if (i + 1 < argc) {
				res.inputfile = argv[i + 1];
			}
		} else if (!strcmp(curarg, "-o")) {
			if (i + 1 < argc) {
				res.outputfile = argv[i + 1];
			}
		} else if (!strcmp(curarg, "-e")) {
			if (i + 1 < argc) {
				res.entrypoint = argv[i + 1];
			}
		} else if (!strcmp(curarg, "-s")) {
			if (i + 1 < argc) {
				res.stage = psbc_stage_from_name(argv[i + 1]);
			}
		} else if (!strcmp(curarg, "-4")) {
			res.target = PSBC_TARGET_PS4_BASE;
		} else if (!strcmp(curarg, "-n")) {
			res.target = PSBC_TARGET_PS4_NEO;
		} else if (!strcmp(curarg, "-g")) {
			res.target = PSBC_TARGET_PS5;
		} else if (!strcmp(curarg, "-Od")) {
			res.optimise = false;
		} else if (!strcmp(curarg, "-vv")) {
			res.verbose = true;
		}
	}

	return res;
}

static inline void showhelp(void) {
	printf(
	    "opengnm-psbc %s\n"
	    "Usage:\n"
	    "\t-f [path] -- The input SPIRV file to compile\n"
	    "\t-o [path] -- The output compiled GCN shader file\n"
	    "\t-e [entrypoint] -- The entrypoint's name (default: \"main\")\n"
	    "\t-s [stage] -- The shader's stage name\n"
	    "\t-Od -- Disable some optimisations\n"
	    "\t-4 -- Target PS4 base (GFX7) instead of PS5\n"
	    "\t-n -- Target PS4 Pro (NEO/GFX8) instead of PS5\n"
	    "\t-g -- Target PS5 (RDNA2/GFX10.3) [default]\n"
	    "\t-vv -- Enable verbose messages output\n"
	    "\t-h -- Show this help message\n",
	    VERSION_STR
	);
}

static inline _Noreturn void fatal(const char* msg) {
	fprintf(stderr, "%s\n", msg);
	exit(EXIT_FAILURE);
}
static inline _Noreturn void fatalf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	putc('\n', stderr);
	exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
	const CmdOptions opts = parsecmdoptions(argc, argv);

	if (opts.showhelp) {
		showhelp();
		return EXIT_SUCCESS;
	}

	if (!opts.inputfile) {
		fatal("Please pass an input file");
	}
	if (!opts.outputfile) {
		fatal("Please pass an output file");
	}
	if (opts.stage == PSBC_STAGE_NONE) {
		fatal("Please pass a valid shader stage");
	}

	FILE* inputhandle = fopen(opts.inputfile, "r");
	if (!inputhandle) {
		fatalf("Failed to open input file with %s", strerror(errno));
	}

	fseek(inputhandle, 0, SEEK_END);
	size_t inputlen = ftell(inputhandle);
	fseek(inputhandle, 0, SEEK_SET);

	void* input = malloc(inputlen);
	if (!input) {
		abort();
	}

	if (fread(input, 1, inputlen, inputhandle) != inputlen) {
		fatalf("Failed to read input file with %s", strerror(errno));
	}

	fclose(inputhandle);

	/* Compile via libpsbc */
	PsbcCompileOptions compile_opts = {
	    .target = opts.target,
	    .stage = opts.stage,
	    .entrypoint = opts.entrypoint,
	    .optimise = opts.optimise,
	};

	PsbcShaderOutput output = {0};
	PsbcResult result = psbc_compile_shader(
	    (const uint32_t*)input, inputlen, &compile_opts, &output
	);

	free(input);

	if (result != PSBC_RESULT_OK) {
		fatalf("Shader compilation failed: %s", psbc_result_string(result));
	}

	/* Write output to file */
	FILE* h = fopen(opts.outputfile, "w");
	if (!h) {
		fatalf("Failed to open output file with %s", strerror(errno));
	}

	if (fwrite(output.data, 1, output.size, h) != output.size) {
		psbc_free_output(&output);
		fclose(h);
		fatalf("Failed to write output file with %s", strerror(errno));
	}

	fclose(h);

	if (opts.verbose) {
		printf("Compiled %s (%s) -> %s (%zu bytes)\n",
		       opts.inputfile, opts.entrypoint, opts.outputfile, output.size);
	}

	psbc_free_output(&output);

	return EXIT_SUCCESS;
}
