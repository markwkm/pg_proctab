ARG PGALTVER
FROM postgres:${PGALTVER}

ARG PGVER
ENV DEBIAN_FRONTEND="noninteractive"
RUN apt-get -qq -y update && \
    apt-get -qq -y dist-upgrade && \
    apt-get -qq -y install gcc \
                           make \
                           postgresql-server-dev-${PGVER} && \
    apt-get -qq -y clean && \
    apt-get -qq -y autoclean

COPY . /usr/local/src/pg_proctab
WORKDIR /usr/local/src/pg_proctab
RUN make install
