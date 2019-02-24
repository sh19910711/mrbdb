.PHONY: run

run:
	docker build -t mrbdb_dev .
	docker run \
		--rm \
		--name mrbdb_dev \
		-ti \
		-w /opt/build \
		-v $(PWD)/script:/opt/build/script \
		-v $(PWD)/src:/opt/mysql-server/storage/mrbdb \
		mrbdb_dev \
		bash
