/* Copyright (c) 2019 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */


#pragma once

#include "base/Base.h"
#include "webservice/Common.h"
#include <proxygen/httpserver/RequestHandler.h>
#include "stats/StatsManager.h"


namespace nebula {

class GetMetricsHandler : public proxygen::RequestHandler {
public:
    GetMetricsHandler() = default;

    void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers)
        noexcept override;

    void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

    void onEOM() noexcept override;

    void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override;

    void requestComplete() noexcept override;

    void onError(proxygen::ProxygenError err) noexcept override;

private:
    HttpCode err_{HttpCode::SUCCEEDED};
    // TODO(shylock) Could expose metrics by name?
    // std::vector<std::string> metricNames_;
    std::unique_ptr<stats::MetricsSerializer> serializer_{nullptr};
};

}  // namespace nebula
