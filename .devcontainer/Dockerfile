ARG VERSION=7

FROM centos:${VERSION}

ARG AREA=C

RUN yum update -y && yum -y install \
    gdb \
    git \
    rpm-build \
    sudo \
    vim \
  && curl -fsSL https://raw.githubusercontent.com/vesoft-inc/nebula/master/build_dep.sh -o /tmp/nebula_build_dep.sh \
  && bash /tmp/nebula_build_dep.sh ${AREA} \
  && yum clean all \
  && rm -rf /var/cache/yum /tmp/nebula_build_dep.sh

RUN mkdir -p /home/nebula

WORKDIR /home/nebula
