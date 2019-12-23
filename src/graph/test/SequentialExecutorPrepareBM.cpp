/* Copyright (c) 2019 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include <folly/Benchmark.h>
#include <parser/GQLParser.h>
#include "graph/SequentialExecutor.h"
#include <boost/preprocessor/repetition/repeat.hpp>

#define Fold(z, n, text)  text

#define STRREP(str, n) BOOST_PP_REPEAT(n, Fold, str)

namespace nebula {
namespace graph {

static ExecutionContext* e = nullptr;

inline void sequentialIter(SequentialSentences* s, const std::size_t iters) {
    auto executor = std::make_unique<SequentialExecutor>(s, e);
    for (auto i = 0U; i < iters; ++i) {
        auto status = executor->prepare();
        if (!status.ok()) {
            abort();
        }
    }
}

const char nGQL_1[] = "INSERT VERTEX dummy(a1, a2) values 1:(3, 4);";
SequentialSentences* s1 = nullptr;
BENCHMARK(SequentialExecutorPrepare_1, iters) {
    sequentialIter(s1, iters);
}

const char nGQL_16[] = STRREP("INSERT VERTEX dummy(a1, a2) values 1:(3, 4);", 16);
SequentialSentences* s16 = nullptr;
BENCHMARK_RELATIVE(SequentialExecutorPrepare_16, iters) {
    sequentialIter(s16, iters);
}

const char nGQL_256[] = STRREP("INSERT VERTEX dummy(a1, a2) values 1:(3, 4);", 256);
SequentialSentences* s256 = nullptr;
BENCHMARK_RELATIVE(SequentialExecutorPrepare_256, iters) {
    sequentialIter(s256, iters);
}

const char nGQL_1k[] = STRREP(STRREP("INSERT VERTEX dummy(a1, a2) values 1:(3, 4);", 32), 32);
SequentialSentences* s1k = nullptr;
BENCHMARK_RELATIVE(SequentialExecutorPrepare_1k, iters) {
    sequentialIter(s1k, iters);
}

}  // namespace graph
}  // namespace nebula


int main(int argc, char** argv) {
    folly::init(&argc, &argv, true);

    std::shared_ptr<apache::thrift::concurrency::ThreadManager> threadManager(
        PriorityThreadManager::newPriorityThreadManager(16, true /*stats*/));
    threadManager->start();
    auto rctx = std::make_unique<nebula::graph::RequestContext<
        nebula::graph::cpp2::ExecutionResponse>>();
//    rctx->setQuery(nGQL);
    rctx->setRunner(threadManager.get());

    auto ectx = std::make_unique<nebula::graph::ExecutionContext>(std::move(rctx),
                                                   nullptr,
                                                   nullptr,
                                                   nullptr,
                                                   nullptr,
                                                   nullptr);
    nebula::graph::e = ectx.get();

    auto sentence1 = ::nebula::GQLParser().parse(nebula::graph::nGQL_1).value();
    nebula::graph::s1 = sentence1.get();
    auto sentence16 = ::nebula::GQLParser().parse(nebula::graph::nGQL_16).value();
    nebula::graph::s16 = sentence16.get();
    auto sentence256 = ::nebula::GQLParser().parse(nebula::graph::nGQL_256).value();
    nebula::graph::s256 = sentence256.get();
    auto sentence1k = ::nebula::GQLParser().parse(nebula::graph::nGQL_1k).value();
    nebula::graph::s1k = sentence1k.get();

    folly::runBenchmarks();
    return 0;
}

