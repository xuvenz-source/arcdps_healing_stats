rule("protobuf")
	set_extensions(".proto")
	before_buildcmd_file(function (target, batchcmds, sourcefile, opt)
		local targetfile = path.filename(sourcefile)
		local basedir = sourcefile:sub(1, - (#targetfile + 1))
		local basename = path.basename(sourcefile)
		local outputdir = target:autogendir()

		if not os.isdir(outputdir) then
			print("Creating output dir "..outputdir)
			os.mkdir(outputdir)
		end

		target:add("includedirs", outputdir)

		local grpc_cpp_plugin = "vcpkg_installed/x64-linux/x64-linux/tools/grpc/grpc_cpp_plugin"
		local protoc = "vcpkg_installed/x64-linux/x64-linux/tools/protobuf/protoc"

		batchcmds:show_progress(opt.progress, "${color.build.object}compiling.proto_source %s", sourcefile)
		batchcmds:vrunv(protoc, {"--cpp_out="..outputdir, "--grpc_out="..outputdir, "--plugin=protoc-gen-grpc="..grpc_cpp_plugin, "--proto_path="..basedir, targetfile})

		local lowest_mtime = nil
		for _, extension in pairs({".pb.cc", ".pb.h", ".grpc.pb.cc", ".grpc.pb.h"}) do
			file = path.join(outputdir, basename..extension)

			local mtime = os.mtime(file)
			if (lowest_mtime == nil) or (mtime < lowest_mtime) then
				lowest_mtime = mtime
			end
		end

		local depcache = target:dependfile(sourcefile.."_source")
		batchcmds:add_depfiles(sourcefile, protoc)
		batchcmds:set_depmtime(lowest_mtime)
		batchcmds:set_depcache(depcache)

		--batchcmds:show("depcache "..depcache.." mtime "..lowest_mtime)
	end)
	on_buildcmd_file(function (target, batchcmds, sourcefile, opt)
		local targetfile = path.filename(sourcefile)
		local basedir = sourcefile:sub(1, - (#targetfile + 1))
		local basename = path.basename(sourcefile)
		local outputdir = target:autogendir()

		local lowest_mtime = nil
		for _, extension in pairs({".pb.cc", ".grpc.pb.cc"}) do
			file = path.join(outputdir, basename..extension)

			local objectfile = target:objectfile(file)
			table.insert(target:objectfiles(), objectfile)

			batchcmds:show_progress(opt.progress, "${color.build.object}compiling.proto_object %s", file)
			batchcmds:compile(file, objectfile)

			local mtime = os.mtime(objectfile)
			if (lowest_mtime == nil) or (mtime < lowest_mtime) then
				lowest_mtime = mtime
			end
		end

		local depcache = target:dependfile(sourcefile.."_object")

		batchcmds:add_depfiles(sourcefile)
		batchcmds:set_depmtime(lowest_mtime)
		batchcmds:set_depcache(depcache)

		--batchcmds:show("depcache "..depcache.." mtime "..lowest_mtime)
	end)

target("evtc_rpc_server")
	add_rules("protobuf")

	set_kind("binary")
	set_warnings("all")
	set_languages("c++20")
	local toolset = "clang++"
	set_toolset("cxx", toolset)
	set_toolset("ld", toolset)

	compilerflags = {}
	if is_mode("debug") then
		add_options("debug")
		add_defines("_DEBUG")

	elseif is_mode("asan") then
		set_optimize("none")
		add_defines("_DEBUG")
		table.insert(compilerflags, "-fsanitize=address")
		add_ldflags("-fsanitize=address")

	elseif is_mode("tsan") then
		set_optimize("none")
		add_defines("_DEBUG")
		table.insert(compilerflags, "-fsanitize=thread")
		add_ldflags("-fsanitize=thread")

	elseif is_mode("ubsan") then
		set_optimize("none")
		add_defines("_DEBUG")
		table.insert(compilerflags, "-fsanitize=undefined")
		table.insert(compilerflags, "-fno-sanitize=alignment")
		add_ldflags("-fsanitize=undefined")
		--add_ldflags("-fno-sanitize=alignment")

	elseif is_mode("release") then
		set_optimize("fastest")
		add_defines("NDEBUG")
		add_cxxflags("-flto")
		add_ldflags("-flto")
		add_ldflags("-fuse-linker-plugin")

	elseif is_mode("profiling") then
		set_optimize("fastest")
		add_defines("NDEBUG")
		add_cxxflags("-flto")
		add_ldflags("-flto")
		add_ldflags("-fuse-linker-plugin")
		add_cxxflags("-pg")
		add_ldflags("-pg")

	end

	add_defines("LINUX")

	add_syslinks("pthread")

	add_includedirs("vcpkg_installed/x64-linux/x64-linux/include")
	add_linkdirs("vcpkg_installed/x64-linux/x64-linux/lib")

	-- Add everything absl as a group since they have circular dependencies (doesn't seem to be the case anymore?)
	add_linkgroups(
		"absl_base",
		"absl_city",
		"absl_civil_time",
		"absl_cord",
		"absl_cord_internal",
		"absl_cordz_functions",
		"absl_cordz_handle",
		"absl_cordz_info",
		"absl_cordz_sample_token",
		"absl_crc32c",
		"absl_crc_cord_state",
		"absl_crc_cpu_detect",
		"absl_crc_internal",
		"absl_debugging_internal",
		"absl_decode_rust_punycode",
		"absl_demangle_internal",
		"absl_demangle_rust",
		"absl_die_if_null",
		"absl_examine_stack",
		"absl_exponential_biased",
		"absl_failure_signal_handler",
		"absl_flags_commandlineflag",
		"absl_flags_commandlineflag_internal",
		"absl_flags_config",
		"absl_flags_internal",
		"absl_flags_marshalling",
		"absl_flags_parse",
		"absl_flags_private_handle_accessor",
		"absl_flags_program_name",
		"absl_flags_reflection",
		"absl_flags_usage",
		"absl_flags_usage_internal",
		"absl_graphcycles_internal",
		"absl_hash",
		"absl_hashtable_profiler",
		"absl_hashtablez_sampler",
		"absl_int128",
		"absl_kernel_timeout_internal",
		"absl_leak_check",
		"absl_log_entry",
		"absl_log_flags",
		"absl_log_globals",
		"absl_log_initialize",
		"absl_log_internal_check_op",
		"absl_log_internal_conditions",
		"absl_log_internal_fnmatch",
		"absl_log_internal_format",
		"absl_log_internal_globals",
		"absl_log_internal_log_sink_set",
		"absl_log_internal_message",
		"absl_log_internal_nullguard",
		"absl_log_internal_proto",
		"absl_log_internal_structured_proto",
		"absl_log_severity",
		"absl_log_sink",
		"absl_malloc_internal",
		"absl_periodic_sampler",
		"absl_poison",
		"absl_profile_builder",
		"absl_random_distributions",
		"absl_random_internal_distribution_test_util",
		"absl_random_internal_entropy_pool",
		"absl_random_internal_platform",
		"absl_random_internal_randen",
		"absl_random_internal_randen_hwaes",
		"absl_random_internal_randen_hwaes_impl",
		"absl_random_internal_randen_slow",
		"absl_random_internal_seed_material",
		"absl_random_seed_gen_exception",
		"absl_random_seed_sequences",
		"absl_raw_hash_set",
		"absl_raw_logging_internal",
		"absl_scoped_set_env",
		"absl_spinlock_wait",
		"absl_stacktrace",
		"absl_status",
		"absl_statusor",
		"absl_strerror",
		"absl_str_format_internal",
		"absl_strings",
		"absl_strings_internal",
		"absl_string_view",
		"absl_symbolize",
		"absl_synchronization",
		"absl_throw_delegate",
		"absl_time",
		"absl_time_zone",
		"absl_tracing_internal",
		"absl_utf8_for_code_point",
		"absl_vlog_config_internal",
		{name = "absl", group = false, static = true})

	-- Add all libraries from vcpkg (just mined from a directory listing, excluding the absl libraries above). Then libraries removed one by one and checking if it still links.
	add_linkgroups(
		"jemalloc",
		"upb",
		"address_sorting",
		"gpr",
		"prometheus-cpp-core",
		"grpc++",
		"prometheus-cpp-pull",
		"crypto",
		"ssl",
		"cares",
		"re2",
		"fmt",
		"utf8_range",
		"utf8_validity",
		"z",
		"protobuf",
		"civetweb-cpp",
		"spdlog",
		"civetweb",
		"grpc",
		{name = "others", group = false, static = true})

	add_files("src/Log.cpp", {cxxflags = compilerflags})
	add_files("evtc_rpc_server/**.cpp", {cxxflags = compilerflags})
	add_files("networking/**.cpp", {cxxflags = compilerflags})
	add_files("networking/**.proto")

	add_cxxflags("-fPIC")
	add_cxxflags("-ggdb3")
	add_cxxflags("-march=native")
	add_cxxflags("-Wextra", "-Weffc++", "-pedantic")
	add_cxxflags("-Werror=ignored-qualifiers", "-Werror=return-type")
	if toolset == "clang++" then
		add_cxxflags("-Werror=shadow")
		add_cxxflags("-Werror=duplicate-decl-specifier")
	end
	add_cxxflags("-Wno-format") -- unsigned long long vs unsigned long issues (linux is stupid...)
	add_cxxflags("-Wno-gnu-zero-variadic-macro-arguments", "-Wno-format-pedantic")
	add_ldflags("-fuse-ld=lld")
