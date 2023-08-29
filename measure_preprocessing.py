#! /usr/bin/env python

import datetime
import os
import shutil
import statistics
import subprocess
from time import perf_counter

log_file_str = "measure_preprocessing_log.txt"
results_file_str = "measure_preprocessing_results.txt"
path_file_str = "../tripbased_performance/paths.h"
aachen_path_str = "/home/mo/uni/thesis/data/input/aachen"
berlin_path_str = "/home/mo/uni/thesis/data/input/berlin"
period_file_str = "../tripbased_performance/periods.h"
types_file_str = "../include/nigiri/types.h"
types_file_copy_str = "../include/nigiri/types.h.copy"
settings_file_str = "../include/nigiri/routing/tripbased/settings.h"
build_dir_str = "build_measure_preprocessing"
c_compiler_str = "-DCMAKE_C_COMPILER=/usr/bin/clang"
cxx_compiler_str = "-DCMAKE_CXX_COMPILER=/usr/bin/clang++"
cxx_flags_str = "-DCMAKE_CXX_FLAGS=\"-fcolor-diagnostics -stdlib=libc++ -mtune=native -march=native\""
generator_str = "-GNinja"
build_type_str = "-DCMAKE_BUILD_TYPE=Release"

aachen_periods_to = ["2021_y / January / 11", "2021_y / March / 11", "2021_y / June / 11", "2021_y / September / 11",
                     "2021_y / December / 11"]
aachen_n_bits = [64, 128, 256, 512, 512]

berlin_periods_to = ["2023_y / July / 9", "2023_y / August / 9", "2023_y / September / 9", "2023_y / October / 9",
                     "2023_y / November / 9", "2023_y / December / 9"]
berlin_n_bits = [64, 64, 128, 128, 256, 256]

n_samples = 10


def result(result2log):
    with open(results_file_str, "a") as result_file:
        result_file.write(result2log + "\n")


def configure():
    reset_ch_build_dir()
    subprocess.run(cmake_cmd_str(), shell=True)


def build():
    subprocess.run("ninja", shell=True)


def write_paths(*, aachen_path, berlin_path):
    file_content_str = "#pragma once\n#include \"nigiri/loader/dir.h\"\nnamespace nigiri::routing::tripbased::performance {\nloader::fs_dir const aachen_dir{\"" + aachen_path + "\"};\nloader::fs_dir const berlin_dir{\"" + berlin_path + "\"};\n}  // namespace nigiri::routing::tripbased::performance"
    with open(path_file_str, "w") as path_file:
        path_file.write(file_content_str)


def write_periods(*, aachen_to, berlin_to):
    file_content_str = "#pragma once\n#include \"nigiri/common/interval.h\"\n#include \"date/date.h\"\nnamespace nigiri::routing::tripbased::performance {\nconstexpr interval<std::chrono::sys_days> aachen_period() {\nusing namespace date;\nconstexpr auto const from = (2020_y / December / 14).operator sys_days();\nconstexpr auto const to = (" + aachen_to + ").operator sys_days();\nreturn {from, to};\n}\nconstexpr interval<std::chrono::sys_days> berlin_period() {\nusing namespace date;\nconstexpr auto const from = (2023_y / June / 15).operator sys_days();\nconstexpr auto const to = (" + berlin_to + ").operator sys_days();\nreturn {from, to};\n}\n}  // namespace nigiri::routing::tripbased::performance"
    with open(period_file_str, "w") as period_file:
        period_file.write(file_content_str)


def write_n_bits(*, n_bits):
    shutil.copyfile(types_file_str, types_file_copy_str)
    target_str = "constexpr auto const kMaxDays = 512;"
    replacement_str = "constexpr auto const kMaxDays = " + str(n_bits) + ";"
    with open(types_file_str, "r") as types_file:
        file_content_str = types_file.read()
    file_content_str = file_content_str.replace(target_str, replacement_str)
    with open(types_file_str, "w") as types_file:
        types_file.write(file_content_str)


def reset_types_file():
    shutil.copyfile(types_file_copy_str, types_file_str)


def write_settings(*, only_load_tt, line_based_pruning, u_turn_removal, transfer_reduction,
                   cache_pressure_reduction, lower_bound_pruning, min_walk, transfer_class):
    file_content_str = "#pragma once\n"

    if (only_load_tt):
        file_content_str += "#define ONLY_LOAD_TT\n"

    file_content_str += "//additional criteria\n"
    if (min_walk):
        file_content_str += "#define TB_MIN_WALK\n"
    elif (transfer_class):
        file_content_str += "#define TB_TRANSFER_CLASS\n"
        file_content_str += "#define TB_TRANSFER_CLASS0 15\n"
        file_content_str += "#define TB_TRANSFER_CLASS1 5\n"

    file_content_str += "// preprocessing options\n"
    if (line_based_pruning):
        file_content_str += "#define TB_PREPRO_LB_PRUNING\n"
    file_content_str += "#ifndef TB_PREPRO_LB_PRUNING\n#define TB_PREPRO_VANILLA\n#endif\n"
    if (u_turn_removal):
        file_content_str += "#define TB_PREPRO_UTURN_REMOVAL\n"
    if (transfer_reduction):
        file_content_str += "#define TB_PREPRO_TRANSFER_REDUCTION\n"

    file_content_str += "// query engine options\n"
    if (cache_pressure_reduction):
        file_content_str += "#define TB_CACHE_PRESSURE_REDUCTION\n"
    if (lower_bound_pruning):
        file_content_str += "#define TB_LOWER_BOUND\n"

    file_content_str += ("// system limits - number of bits\n#define BITFIELD_IDX_BITS 25U\n#define TRANSPORT_IDX_BITS "
                         "26U\n#define STOP_IDX_BITS 10U\n#define DAY_IDX_BITS 9U\n#define NUM_TRANSFERS_BITS "
                         "5U\n#define DAY_OFFSET_BITS 3U\n// the position of the query day in the day offset\n#define "
                         "QUERY_DAY_SHIFT 5\nnamespace nigiri::routing::tripbased {\nconstexpr unsigned const "
                         "kBitfieldIdxMax = 1U << BITFIELD_IDX_BITS;\nconstexpr unsigned const kTransportIdxMax = 1U "
                         "<< TRANSPORT_IDX_BITS;\nconstexpr unsigned const kStopIdxMax = 1U << "
                         "STOP_IDX_BITS;\nconstexpr unsigned const kDayIdxMax = 1U << DAY_IDX_BITS;\nconstexpr "
                         "unsigned const kNumTransfersMax = 1U << NUM_TRANSFERS_BITS;\nconstexpr unsigned const "
                         "kDayOffsetMax = 1U << DAY_OFFSET_BITS;\n}  // namespace nigiri::routing::tripbased")

    with open(settings_file_str, "w") as settings_file:
        settings_file.write(file_content_str)


def reset_ch_build_dir():
    if os.path.exists(build_dir_str):
        shutil.rmtree(build_dir_str)
    os.makedirs(build_dir_str)
    os.chdir(build_dir_str)


def cmake_cmd_str():
    return "cmake " + c_compiler_str + " " + cxx_compiler_str + " " + cxx_flags_str + " " + generator_str + " " + build_type_str + " .."


def measure_aachen():
    for (aachen_to, n_bits) in zip(aachen_periods_to, aachen_n_bits):
        write_periods(aachen_to=aachen_to, berlin_to=berlin_periods_to[0])
        write_n_bits(n_bits=n_bits)
        build()
        result("aachen - until " + aachen_to + ", n_bits: " + str(n_bits))
        samples = []
        for i in range(0, n_samples):
            start = perf_counter()
            subprocess.run("./aachen", shell=True)
            samples.append(perf_counter() - start)
            result(str(samples[i]))
        result("average of " + str(n_samples) + " runs: " + str(statistics.mean(samples)))
        reset_types_file()


def measure_berlin():
    for (berlin_to, n_bits) in zip(berlin_periods_to, berlin_n_bits):
        write_periods(aachen_to=aachen_periods_to[0], berlin_to=berlin_to)
        write_n_bits(n_bits=n_bits)
        build()
        result("berlin - until " + berlin_to + ", n_bits: " + str(n_bits))
        samples = []
        for i in range(0, n_samples):
            start = perf_counter()
            subprocess.run("./berlin", shell=True)
            samples.append(perf_counter() - start)
            result(str(samples[i]))
        result("average of " + str(n_samples) + " runs: " + str(statistics.mean(samples)))
        reset_types_file()


def main():
    configure()

    init_time_str = datetime.datetime.now().strftime("%Y-%b-%d %H:%M:%S")
    result("Results from " + init_time_str)

    # baseline: load timetable without trip-based preprocessing
    write_settings(only_load_tt=True, line_based_pruning=False, u_turn_removal=False, transfer_reduction=False,
                   cache_pressure_reduction=False, lower_bound_pruning=False, min_walk=False, transfer_class=False)
    result("baseline aachen")
    measure_aachen()
    result("baseline berlin")
    measure_berlin()

    # line-based pruning: OFF, U-turn removal: OFF, transfer reduction: OFF
    write_settings(only_load_tt=False, line_based_pruning=False, u_turn_removal=False, transfer_reduction=False,
                   cache_pressure_reduction=False, lower_bound_pruning=False, min_walk=False, transfer_class=False)
    result("vanilla aachen")
    measure_aachen()
    result("vanilla berlin")
    measure_berlin()

    # line-based pruning: OFF, U-turn removal: ON, transfer reduction: OFF
    write_settings(only_load_tt=False, line_based_pruning=False, u_turn_removal=True, transfer_reduction=False,
                   cache_pressure_reduction=False, lower_bound_pruning=False, min_walk=False, transfer_class=False)
    result("vanilla u-turn aachen")
    measure_aachen()
    result("vanilla u-turn berlin")
    measure_berlin()

    # line-based pruning: OFF, U-turn removal: ON, transfer reduction: ON
    write_settings(only_load_tt=False, line_based_pruning=False, u_turn_removal=True, transfer_reduction=True,
                   cache_pressure_reduction=False, lower_bound_pruning=False, min_walk=False, transfer_class=False)
    result("vanilla u-turn transfer reduction aachen")
    measure_aachen()
    result("vanilla u-turn transfer reduction berlin")
    measure_berlin()

    # line-based pruning: ON, U-turn removal: ON, transfer reduction: OFF
    write_settings(only_load_tt=False, line_based_pruning=True, u_turn_removal=True, transfer_reduction=False,
                   cache_pressure_reduction=False, lower_bound_pruning=False, min_walk=False, transfer_class=False)
    result("line-based pruning u-turn aachen")
    measure_aachen()
    result("line-based pruning u-turn berlin")
    measure_berlin()

    # line-based pruning: ON, U-turn removal: ON, transfer reduction: ON
    write_settings(only_load_tt=False, line_based_pruning=True, u_turn_removal=True, transfer_reduction=False,
                   cache_pressure_reduction=False, lower_bound_pruning=False, min_walk=False, transfer_class=False)
    result("line-based pruning u-turn transfer reduction aachen")
    measure_aachen()
    result("line-based pruning u-turn transer reduction berlin")
    measure_berlin()


if __name__ == "__main__":
    main()
