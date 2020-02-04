FROM ubuntu:17.04
RUN apt-get update
RUN apt-get -y install wget sudo python
COPY install_kth.sh /
RUN chmod 755 /install_kth.sh
RUN /install_kth.sh
RUN /kth/bin/bn --initchain
EXPOSE 8333
ENTRYPOINT ["/kth/bin/bn"]
