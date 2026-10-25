# Captive portal as an isolated container: a bug in passenger-facing HTTP
# code cannot reach eCall, the modem or the keystore.
FROM python:3.12-alpine3.20

RUN apk add --no-cache nftables

COPY src/hotspot/hotspot_policy.py src/hotspot/captive_portal.py /opt/portal/
WORKDIR /opt/portal

# Runs as container root because nft needs an effective CAP_NET_ADMIN;
# compose drops every other capability and the rootfs is read-only.
EXPOSE 8080
ENTRYPOINT ["python3", "-I", "/opt/portal/captive_portal.py"]
CMD ["--bind", "192.168.50.1", "--port", "8080"]
