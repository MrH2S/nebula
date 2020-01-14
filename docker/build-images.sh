#!/usr/bin/env bash
#
# Copyright (c) 2019 vesoft inc. All rights reserved.
#
# This source code is licensed under Apache 2.0 License,
# attached with Common Clause Condition 1.0, found in the LICENSES directory.

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"/../

for suffix in graphd; do
    docker build -t vesoft/nebula-$suffix:nightly -f $PROJECT_DIR/docker/Dockerfile.$suffix $PROJECT_DIR
done
