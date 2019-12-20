/* Copyright (c) 2018 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "base/Base.h"
#include "stats/StatsManager.h"
#include <folly/stats/MultiLevelTimeSeries-defs.h>
#include <folly/stats/TimeseriesHistogram-defs.h>

namespace nebula {
namespace stats {

// static
StatsManager& StatsManager::get() {
    static StatsManager sm;
    return sm;
}


// static
void StatsManager::setDomain(folly::StringPiece domain) {
    get().domain_ = domain.toString();
}


// static
void StatsManager::setReportInfo(HostAddr addr, int32_t interval) {
    auto& sm = get();
    sm.collectorAddr_ = addr;
    sm.interval_ = interval;
}


// static
int32_t StatsManager::registerStats(folly::StringPiece counterName) {
    using std::chrono::seconds;

    auto& sm = get();

    std::string name = counterName.toString();
    auto it = sm.nameMap_.find(name);
    if (it != sm.nameMap_.end()) {
        LOG(INFO) << "The counter \"" << name << "\" already exists";
        return it->second;
    }

    // Insert the Stats
    sm.stats_.emplace_back(
        std::make_pair(
            std::make_unique<std::mutex>(),
            std::make_unique<StatsType>(
                60,
                std::initializer_list<StatsType::Duration>({seconds(60),
                                                            seconds(600),
                                                            seconds(3600)}))));
    int32_t index = sm.stats_.size();
    sm.nameMap_[name] = index;
    auto parsedName = parseMetricName(counterName);
    if (parsedName.ok()) {
        sm.idParsedName_[index] = std::make_unique<ParsedName>(parsedName.value());
    } else {
        sm.idParsedName_[index] = nullptr;
    }
    return index;
}


// static
int32_t StatsManager::registerHisto(folly::StringPiece counterName,
                                    StatsManager::VT bucketSize,
                                    StatsManager::VT min,
                                    StatsManager::VT max) {
    LOG(INFO) << "registerHisto, bucketSize: " << bucketSize
              << ", min: " << min << ", max: " << max;
    using std::chrono::seconds;

    auto& sm = get();
    std::string name = counterName.toString();
    auto it = sm.nameMap_.find(name);
    if (it != sm.nameMap_.end()) {
        LOG(ERROR) << "The counter \"" << name << "\" already exists";
        return it->second;
    }

    // Insert the Histogram
    sm.histograms_.emplace_back(
        std::make_pair(
            std::make_unique<std::mutex>(),
            std::make_unique<HistogramType>(
                bucketSize,
                min,
                max,
                StatsType(60, {seconds(60), seconds(600), seconds(3600)}))));
    int32_t index = - sm.histograms_.size();
    sm.nameMap_[name] = index;
    auto parsedName = parseMetricName(counterName);
    if (parsedName.ok()) {
        sm.idParsedName_[index] = std::make_unique<ParsedName>(parsedName.value());
    } else {
        sm.idParsedName_[index] = nullptr;
    }
    return index;
}


// static
void StatsManager::addValue(int32_t index, VT value) {
    using std::chrono::seconds;
    CHECK_NE(index, 0);

    auto& sm = get();
    if (index > 0) {
        // Stats
        --index;
        DCHECK_LT(index, sm.stats_.size());
        std::lock_guard<std::mutex> g(*(sm.stats_[index].first));
        sm.stats_[index].second->addValue(seconds(time::WallClock::fastNowInSec()), value);
    } else {
        // Histogram
        index = - (index + 1);
        DCHECK_LT(index, sm.histograms_.size());
        std::lock_guard<std::mutex> g(*(sm.histograms_[index].first));
        sm.histograms_[index].second->addValue(seconds(time::WallClock::fastNowInSec()), value);
    }
}


// static
StatusOr<StatsManager::VT> StatsManager::readValue(folly::StringPiece metricName) {
    std::vector<std::string> parts;
    folly::split(".", metricName, parts, true);
    if (parts.size() != 3) {
        LOG(ERROR) << "\"" << metricName << "\" is not a valid metric name";
        return Status::Error("\"%s\" is not a valid metric name", metricName.data());
    }

    TimeRange range;
    if (parts[2] == "60") {
        range = TimeRange::ONE_MINUTE;
    } else if (parts[2] == "600") {
        range = TimeRange::TEN_MINUTES;
    } else if (parts[2] == "3600") {
        range = TimeRange::ONE_HOUR;
    } else {
        // Unsupported time range
        LOG(ERROR) << "Unsupported time range \"" << parts[2] << "\"";
        return Status::Error(folly::stringPrintf("Unsupported time range \"%s\"",
                                                 parts[2].c_str()));
    }

    // Now check the statistic method
    static int32_t dividors[] = {1, 1, 10, 100, 1000, 10000};
    folly::toLowerAscii(parts[1]);
    if (parts[1] == "sum") {
        return readStats(parts[0], range, StatsMethod::SUM);
    } else if (parts[1] == "count") {
        return readStats(parts[0], range, StatsMethod::COUNT);
    } else if (parts[1] == "avg") {
        return readStats(parts[0], range, StatsMethod::AVG);
    } else if (parts[1] == "rate") {
        return readStats(parts[0], range, StatsMethod::RATE);
    } else if (parts[1][0] == 'p') {
        // Percentile
        try {
            size_t len = parts[1].size()  - 1;
            if (len > 0 && len <= 6) {
                auto digits = folly::StringPiece(&(parts[1][1]), len);
                auto pct = folly::to<double>(digits) / dividors[len - 1];
                return readHisto(parts[0], range, pct);
            }
        } catch (const std::exception& ex) {
            LOG(ERROR) << "Failed to convert the digits to a double: " << ex.what();
        }

        LOG(ERROR) << "\"" << parts[1] << "\" is not a valid percentile form";
        return Status::Error(folly::stringPrintf("\"%s\" is not a valid percentile form",
                                                 parts[1].c_str()));
    } else {
        LOG(ERROR) << "Unsupported statistic method \"" << parts[1] << "\"";
        return Status::Error(folly::stringPrintf("Unsupported statistic method \"%s\"",
                                                 parts[1].c_str()));
    }
}


// static
void StatsManager::readAllValue(folly::dynamic& vals) {
    auto& sm = get();

    for (auto &statsName : sm.nameMap_) {
        for (auto method = StatsMethod::SUM; method <= StatsMethod::RATE;
             method = static_cast<StatsMethod>(static_cast<int>(method) + 1)) {
            for (auto range = TimeRange::ONE_MINUTE; range <= TimeRange::ONE_HOUR;
                 range = static_cast<TimeRange>(static_cast<int>(range) + 1)) {
                std::string metricName = statsName.first;
                auto status = readStats(statsName.second, range, method);
                CHECK(status.ok());
                int64_t metricValue = status.value();
                folly::dynamic stat = folly::dynamic::object();

                switch (method) {
                    case StatsMethod::SUM:
                        metricName += ".sum";
                        break;
                    case StatsMethod::COUNT:
                        metricName += ".count";
                        break;
                    case StatsMethod::AVG:
                        metricName += ".avg";
                        break;
                   case StatsMethod::RATE:
                        metricName += ".rate";
                        break;
                    // intentionally no `default'
                }

                switch (range) {
                    case TimeRange::ONE_MINUTE:
                        metricName += ".60";
                        break;
                    case TimeRange::TEN_MINUTES:
                        metricName += ".600";
                        break;
                    case TimeRange::ONE_HOUR:
                        metricName += ".3600";
                        break;
                    // intentionally no `default'
                }

                stat["name"] = metricName;
                stat["value"] = metricValue;
                vals.push_back(std::move(stat));
            }
        }
    }
}


// static
StatusOr<StatsManager::VT> StatsManager::readStats(int32_t index,
                                         StatsManager::TimeRange range,
                                         StatsManager::StatsMethod method) {
    using std::chrono::seconds;
    auto& sm = get();

    if (index == 0) {
        return Status::Error("Invalid stats");
    }

    if (index > 0) {
        // stats
        --index;
        DCHECK_LT(index, sm.stats_.size());
        std::lock_guard<std::mutex> g(*(sm.stats_[index].first));
        sm.stats_[index].second->update(seconds(time::WallClock::fastNowInSec()));
        return readValue(*(sm.stats_[index].second), range, method);
    } else {
        // histograms_
        index = - (index + 1);
        DCHECK_LT(index, sm.histograms_.size());
        std::lock_guard<std::mutex> g(*(sm.histograms_[index].first));
        sm.histograms_[index].second->update(seconds(time::WallClock::fastNowInSec()));
        return readValue(*(sm.histograms_[index].second), range, method);
    }
}


// static
StatusOr<StatsManager::VT> StatsManager::readStats(const std::string& counterName,
                                                   StatsManager::TimeRange range,
                                                   StatsManager::StatsMethod method) {
    auto& sm = get();

    // Look up the counter name
    int32_t index = 0;

    {
        auto it = sm.nameMap_.find(counterName);
        if (it == sm.nameMap_.end()) {
            // Not found
            return Status::Error("Stats not found \"%s\"", counterName.c_str());
        }

        index = it->second;
    }

    return readStats(index, range, method);
}


// static
StatusOr<StatsManager::VT> StatsManager::readHisto(const std::string& counterName,
                                                   StatsManager::TimeRange range,
                                                   double pct) {
    using std::chrono::seconds;
    auto& sm = get();

    // Look up the counter name
    int32_t index = 0;
    {
        auto it = sm.nameMap_.find(counterName);
        if (it == sm.nameMap_.end()) {
            // Not found
            return Status::Error("Stats not found \"%s\"", counterName.c_str());
        }

        index = it->second;
    }

    if (index >= 0) {
        return Status::Error("Invalid stats");
    }
    index = - (index + 1);
    if (static_cast<size_t>(index) >= sm.histograms_.size()) {
        return Status::Error("Invalid stats");
    }

    std::lock_guard<std::mutex> g(*(sm.histograms_[index].first));
    sm.histograms_[index].second->update(seconds(time::WallClock::fastNowInSec()));
    auto level = static_cast<size_t>(range);
    return sm.histograms_[index].second->getPercentileEstimate(pct, level);
}

/*static */
StatusOr<StatsManager::VT> StatsManager::readHisto(const size_t index,
        StatsManager::TimeRange range,
        double pct) {
    auto& sm = get();
    std::lock_guard<std::mutex> g(*(sm.histograms_[physicalHistoIndex(index)].first));
    if (physicalHistoIndex(index) >= sm.histograms_.size()) {
        return Status::Error("Out of size.");
    }
    sm.histograms_[physicalHistoIndex(index)].second->update(
        std::chrono::seconds(time::WallClock::fastNowInSec()));
    auto level = static_cast<size_t>(range);
    return sm.histograms_[physicalHistoIndex(index)].second->getPercentileEstimate(pct, level);
}


// static
StatusOr<StatsManager::ParsedName>
StatsManager::parseMetricName(folly::StringPiece metricName) {
    std::vector<std::string> parts;
    folly::split(".", metricName, parts, true);
    if (parts.size() != 3) {
        std::string err = folly::stringPrintf(
            "\"%s\" is not a valid metric name", metricName.data());
        LOG(ERROR) << err;
        return Status::Error(std::move(err));
    }

    std::string name = parts[0];
    TimeRange range;
    if (parts[2] == "60") {
        range = TimeRange::ONE_MINUTE;
    } else if (parts[2] == "600") {
        range = TimeRange::TEN_MINUTES;
    } else if (parts[2] == "3600") {
        range = TimeRange::ONE_HOUR;
    } else {
        // Unsupported time range
        std::string err = folly::stringPrintf("Unsupported time range \"%s\"", parts[2].c_str());
        return Status::Error(std::move(err));
    }

    StatsMethod method;
    folly::toLowerAscii(parts[1]);
    if (parts[1] == "sum") {
        method = StatsMethod::SUM;
    } else if (parts[1] == "count") {
        method = StatsMethod::COUNT;
    } else if (parts[1] == "avg") {
        method = StatsMethod::AVG;
    } else if (parts[1] == "rate") {
        method = StatsMethod::RATE;
    // } else if (parts[1][0] == 'p') {   // TODO(shylock)
    } else {
        std::string err = folly::stringPrintf(
            "Unsupported statistic method \"%s\"", parts[1].c_str());
        LOG(ERROR) << err;
        return Status::Error(std::move(err));
    }

    return StatsManager::ParsedName {
        name,
        method,
        range,
    };
}


// The raw metric format based on JSON
// 1. Gauge, time serial value
// 2. Histogram, time serial value distribution
// The raw data can be transformed to multiple specified foramt (various user-defined metric format)
// E.G.
// {
//     "name": "meta",
//     "gauges": [...],
//     "histograms:" [...],
// }
// Gauge
// {
//     "name": "xxxxx",
//     "value": 33,
//     "labels": [
//         {"name": "name", "value": "nebula"},
//         {"name": "type", "value": "qps"}
//     ]
// }
// Histogram
// {
//     "name": "xxxxx",
//     "value_range": [0, 100],
//     "buckets": [2, 3, 0, 11, ...],
//     "sum": 233,
//     "count": 332,
//     "labels": [
//         {"name": "name", "value": "nebula"},
//         {"name": "type", "value": "latency"}
//     ]
// }
//
/*static*/ std::string StatsManager::toJson() {
    constexpr char kGauges[] = "gauges";
    constexpr char kHistograms[] = "histograms";
    auto& sm = get();
    folly::dynamic obj = folly::dynamic::object(kGauges, folly::dynamic::array())
        (kHistograms, folly::dynamic::array());
    folly::dynamic labels = folly::dynamic::array(
        folly::dynamic(folly::dynamic::object("name", "root")("value", "nebula")));

    // insert
    for (auto& index : sm.nameMap_) {
        auto parsedName = sm.idParsedName_[index.second].get();
        std::string name = parsedName != nullptr ? parsedName->name : index.first;
        if (StatsManager::isStatIndex(index.second)) {
            auto result = StatsManager::readStats(index.second,
                    StatsManager::TimeRange::ONE_MINUTE,
                    StatsManager::StatsMethod::RATE);
            if (!result.ok()) {
                LOG(ERROR) << "Failed read stats value of " << name << " : " << result.status();
                continue;
            }
            folly::dynamic gauge = folly::dynamic::object("name", name)
                    ("value", result.value())
                    ("labels", labels);
            obj[kGauges].push_back(std::move(gauge));
        } else if (StatsManager::isHistoIndex(index.second)) {
            // Expose the metrics computed from Histogram instead of the whole distribution
            // Now we only expose the p99 metrics for simpler
            auto result = readHisto(index.second, StatsManager::TimeRange::ONE_HOUR, 99);
            if (!result.ok()) {
                LOG(ERROR) << "Failed read histogram metrics of " << name << " : "
                    << result.status();
                continue;
            }
            folly::dynamic gauge = folly::dynamic::object("name", name+"_p99")
                    ("value", result.value())
                    ("labels", labels);
            obj[kGauges].push_back(std::move(gauge));
        }
    }
    return folly::toJson(obj);
}


}  // namespace stats
}  // namespace nebula

