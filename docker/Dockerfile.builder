FROM espressif/idf:v5.5.2

RUN apt-get update \
    && apt-get install -y --no-install-recommends clang-format clang-tidy \
    && rm -rf /var/lib/apt/lists/* \
    && /opt/esp/python_env/idf5.5_py3.12_env/bin/pip install --no-cache-dir ruff

RUN git config --global --add safe.directory '*'

ENV POOFER_IDF_PY=idf.py
ENV POOFER_LLVM_BIN=/usr/bin

WORKDIR /project

COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
CMD ["build"]
