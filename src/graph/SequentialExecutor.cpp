/* Copyright (c) 2018 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "base/Base.h"
#include "graph/SequentialExecutor.h"
#include "graph/GoExecutor.h"
#include "graph/PipeExecutor.h"
#include "graph/UseExecutor.h"

namespace nebula {
namespace graph {

SequentialExecutor::SequentialExecutor(SequentialSentences *sentences,
                                       ExecutionContext *ectx) : Executor(ectx) {
    sentences_ = sentences;
}


Status SequentialExecutor::prepare() {
    const std::size_t numExecutor = sentences_->sentences_.size();
    executors_.resize(numExecutor);
    std::vector<folly::SemiFuture<Status>> fResult;
    fResult.reserve(numExecutor);
    for (auto i = 0U; i < numExecutor; ++i) {
        folly::Promise<Status> p;
        fResult.emplace_back(std::move(p.getSemiFuture()));
        auto task = [this, i, p = std::move(p)]() mutable {
            auto *sentence = sentences_->sentences_[i].get();
            auto executor = makeExecutor(sentence);
            if (executor == nullptr) {
                constexpr char err[] = "The statement has not been implemented";
                LOG(ERROR) << err;
                p.setValue(Status::Error(err));
                return;
            }
            // Run prepare concurrently here with protection
            // So make sure the prepare not modify the shared states I.E. Storage
            // In fact, the prepare is used to initialize the members aacording to sentence
            auto status = executor->prepare();
            if (!status.ok()) {
                FLOG_ERROR("Prepare executor `%s' failed: %s",
                            executor->name(), status.toString().c_str());
                p.setValue(std::move(status));
                return;
            }
            executors_[i] = std::move(executor);
            p.setValue(Status::OK());
        };
        ectx()->rctx()->runner()->add(std::move(task));
    }
    auto prepareStatus = folly::collect(fResult).via(ectx()->rctx()->runner())
        .thenValue([](auto&& results) {
            for (auto r : results) {
                if (!r.ok()) {
                    return std::move(r);
                }
            }
            return Status::OK();
        }).thenError([](auto&& e) {
            LOG(ERROR) << e.what();
            return Status::Error(e.what());
        }).get();
    if (!prepareStatus.ok()) {
        return prepareStatus;
    }
    /**
     * For the time being, we execute sentences one by one. We may allow concurrent
     * or out of order execution in the future.
     */
    // For an executor except the last one, it executes the next one on finished.
    // If any fails, the whole execution would abort.
    auto onError = [this] (Status status) {
        DCHECK(onError_);
        onError_(std::move(status));
    };
    for (auto i = 0U; i < executors_.size() - 1; i++) {
        auto onFinish = [this, current = i, next = i + 1] (Executor::ProcessControl ctr) {
            switch (ctr) {
                case Executor::ProcessControl::kReturn: {
                    DCHECK(onFinish_);
                    respExecutorIndex_ = current;
                    onFinish_(ctr);
                    break;
                }
                case Executor::ProcessControl::kNext:
                default: {
                    executors_[next]->execute();
                    break;
                }
            }
        };
        executors_[i]->setOnFinish(onFinish);
        executors_[i]->setOnError(onError);
    }
    // The whole execution is done upon the last executor finishes.
    auto onFinish = [this] (Executor::ProcessControl ctr) {
        DCHECK(onFinish_);
        respExecutorIndex_ = executors_.size() - 1;
        onFinish_(ctr);
    };
    executors_.back()->setOnFinish(onFinish);
    executors_.back()->setOnError(onError);

    return Status::OK();
}


void SequentialExecutor::execute() {
    executors_.front()->execute();
}


void SequentialExecutor::setupResponse(cpp2::ExecutionResponse &resp) {
    executors_[respExecutorIndex_]->setupResponse(resp);
}

}   // namespace graph
}   // namespace nebula
