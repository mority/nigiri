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
vbb_path_str = "/home/mo/uni/thesis/data/input/vbb"
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

default_settings = {"max_days": 512,
                    "aachen_to": "(2021_y / December / 11)",
                    "vbb_to": "(2023_y / December / 9)",
                    "germany_to": "(2019_y / December / 14)",
                    "only_load_tt": False,
                    "line_based_pruning": False,
                    "u_turn_removal": False,
                    "transfer_reduction": False,
                    "cache_pressure_reduction": False,
                    "lower_bound_pruning": False,
                    "min_walk": False,
                    "transfer_class": False}


def settings_str(s):
    result_str = "| "
    for key, value in s.items():
        result_str += " " + key + ": " + str(value) + " |"
    return result_str


n_bits_list = [64, 128, 256, 384, 512]

aachen_periods_to = ["2021_y / February / 11", "2021_y / April / 16", "2021_y / August / 22", "2021_y / December / 11",
                     "2021_y / December / 11"]
vbb_periods_to = ["2023_y / February / 6", "2023_y / April / 11", "2023_y / August / 17", "2023_y / December / 9",
                  "2023_y / December / 9"]
germany_periods_to = ["2019_y / February / 6", "2019_y / April / 11", "2019_y / August / 17", "2019_y / December / 14",
                      "2023_y / December / 14"]

n_samples = 1


def log(text2log):
    with open(log_file_str, "a") as log_file:
        log_file.write(text2log + "\n")


def result(result2log):
    with open(results_file_str, "a") as result_file:
        result_file.write(result2log + "\n")


def configure():
    reset_ch_build_dir()
    subprocess.run(cmake_cmd_str(), shell=True, check=True)


def build():
    subprocess.run("ninja", shell=True, check=True)


def write_paths(*, aachen_path, vbb_path):
    file_content_str = "#pragma once\n#include \"nigiri/loader/dir.h\"\nnamespace nigiri::routing::tripbased::performance {\nloader::fs_dir const aachen_dir{\"" + aachen_path + "\"};\nloader::fs_dir const vbb_dir{\"" + vbb_path + "\"};\n}  // namespace nigiri::routing::tripbased::performance"
    with open(path_file_str, "w") as path_file:
        path_file.write(file_content_str)


def write_settings(s):
    file_content_str = "#pragma once\n// timetable length\n"
    file_content_str += "#define MAX_DAYS " + str(s["max_days"]) + "\n"
    file_content_str += "#define AACHEN_TO (" + s["aachen_to"] + ")\n"
    file_content_str += "#define VBB_TO (" + s["vbb_to"] + ")\n"
    file_content_str += "#define GERMANY_TO (" + s["germany_to"] + ")\n"
    if (s["only_load_tt"]):
        file_content_str += "#define ONLY_LOAD_TT\n"

    file_content_str += "//additional criteria\n"
    if (s["min_walk"]):
        file_content_str += "#define TB_MIN_WALK\n"
    elif (s["transfer_class"]):
        file_content_str += "#define TB_TRANSFER_CLASS\n"
        file_content_str += "#define TB_TRANSFER_CLASS0 15\n"
        file_content_str += "#define TB_TRANSFER_CLASS1 5\n"

    file_content_str += "// preprocessing options\n"
    if (s["line_based_pruning"]):
        file_content_str += "#define TB_PREPRO_LB_PRUNING\n"
    file_content_str += "#ifndef TB_PREPRO_LB_PRUNING\n#define TB_PREPRO_VANILLA\n#endif\n"
    if (s["u_turn_removal"]):
        file_content_str += "#define TB_PREPRO_UTURN_REMOVAL\n"
    if (s["transfer_reduction"]):
        file_content_str += "#define TB_PREPRO_TRANSFER_REDUCTION\n"

    file_content_str += "// query engine options\n"
    if (s["cache_pressure_reduction"]):
        file_content_str += "#define TB_CACHE_PRESSURE_REDUCTION\n"
    if (s["lower_bound_pruning"]):
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
    build()
    samples = []
    result("aachen")
    for i in range(0, n_samples):
        start = perf_counter()
        with open(log_file_str, "a") as log_file:
            subprocess.run("./aachen", shell=True, check=True, stderr=subprocess.STDOUT, stdout=log_file)
        samples.append(perf_counter() - start)
        result(str(samples[i]))
    if n_samples > 1:
        result("average of " + str(n_samples) + " runs: " + str(statistics.mean(samples)))


def measure_vbb():
    build()
    samples = []
    result("vbb")
    for i in range(0, n_samples):
        start = perf_counter()
        with open(log_file_str, "a") as log_file:
            subprocess.run("./vbb", shell=True, check=True, stderr=subprocess.STDOUT, stdout=log_file)
        samples.append(perf_counter() - start)
        result(str(samples[i]))
    if n_samples > 1:
        result("average of " + str(n_samples) + " runs: " + str(statistics.mean(samples)))


def measure_germany():
    build()
    samples = []
    result("germany")
    for i in range(0, n_samples):
        start = perf_counter()
        with open(log_file_str, "a") as log_file:
            subprocess.run("./germany", shell=True, check=True, stderr=subprocess.STDOUT, stdout=log_file)
        samples.append(perf_counter() - start)
        result(str(samples[i]))
    if n_samples > 1:
        result("average of " + str(n_samples) + " runs: " + str(statistics.mean(samples)))


def main():
    configure()

    init_time_str = datetime.datetime.now().strftime("%Y-%b-%d %H:%M:%S")
    result("Results from " + init_time_str)
    log("Log Begin: " + init_time_str)

    # baseline: load timetable without trip-based preprocessing
    settings = default_settings.copy()
    settings["only_load_tt"] = True
    for n_bits, aachen_to, vbb_to, germany_to in zip(n_bits_list, aachen_periods_to, vbb_periods_to,
                                                     germany_periods_to):
        settings["max_days"] = n_bits
        settings["aachen_to"] = aachen_to
        settings["vbb_to"] = vbb_to
        settings["germany_to"] = germany_to
        result(settings_str(settings))
        write_settings(settings)
        measure_aachen()
        measure_vbb()
        measure_germany()

    # line-based pruning: OFF, U-turn removal: OFF, transfer reduction: OFF

    # line-based pruning: OFF, U-turn removal: ON, transfer reduction: OFF

    # line-based pruning: OFF, U-turn removal: ON, transfer reduction: ON

    # line-based pruning: ON, U-turn removal: ON, transfer reduction: OFF

    # line-based pruning: ON, U-turn removal: ON, transfer reduction: ON


if __name__ == "__main__":
    main()
