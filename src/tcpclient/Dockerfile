FROM alpine:latest
RUN mkdir -p /app

RUN apk add --no-cache build-base make

COPY . /app
WORKDIR /app/libs
RUN /bin/sh ./build.sh
WORKDIR /app
RUN make

CMD ["/app/tcpclient"]
