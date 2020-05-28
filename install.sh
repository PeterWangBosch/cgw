mkdir /data/vendor/bin/ -p
mkdir /data/etc/orchestrator/ -p
mkdir /data/var/orchestrator/ -p
chmod 777 /data/var/orchestrator/

cp orchestrator /data/vendor/bin/
cp config.ini /data/etc/orchestrator/

echo "Orchestrator has been installed in /data/vendor/bin/"
