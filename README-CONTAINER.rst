The container will build and install pg_proctab for a specific version of
PostgreSQL.  See the PostgreSQL container documentation for details
instructions on starting the container: https://hub.docker.com/_/postgres/

Quick Instructions
------------------

Create the container for PostgreSQL 15::

    tools/build-container 15

Start PostgreSQL in the container, exposing port 5432::

    podman run -p 5432:5432 --name some-postgres \
            -e POSTGRES_PASSWORD=mysecretpassword -d pg_proctab:15

Connect to PostgreSQL and install **pg_proctab**::

    podman exec -it -u postgres some-postgres /bin/psql \
            -c "CREATE EXTENSION pg_proctab"

Connect to PostgreSQL from the host::

    psql -h localhost postgres postgres
