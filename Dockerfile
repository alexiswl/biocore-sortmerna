
FROM ubuntu:xenial

ARG SORTMERNA_REPO="https://github.com/biocore/sortmerna"
ARG SORTMERNA_VERSION="4.2.0"
ENV PATH="/opt/bin:$PATH"
ENV RRNA_REF_DIR="/opt/rRNA_databases"

RUN ( echo "APT installations" && \
      apt-get update -qq && \
      apt-get install -y -qq \
         wget \
         rsync \
         git && \
      echo "Successfully installed apt installations, now cleaning up" && \
      apt-get clean -qq \
    ) && \
    ( echo "Installing sortmerna" && \
      wget --quiet "${SORTMERNA_REPO}/releases/download/v${SORTMERNA_VERSION}/sortmerna-${SORTMERNA_VERSION}-Linux.sh" && \
      bash "sortmerna-${SORTMERNA_VERSION}-Linux.sh" --prefix="/opt" --skip-license && \
      rm "sortmerna-${SORTMERNA_VERSION}-Linux.sh" && \
      echo "Installation of binary complete" \
    ) && \
    ( echo "Downloading just the rRNA databases from repo" && \
      mkdir sortmerna && \
      cd sortmerna && \
      git init --quiet && \
      git remote add origin --fetch "${SORTMERNA_REPO}" && \
      git config core.sparseCheckout true && \
      echo "data/rRNA_databases/*.fasta" >> .git/info/sparse-checkout && \
      echo "data/rRNA_databases/*.tar.gz" >> .git/info/sparse-checkout && \
      git pull origin master --quiet && \
      git checkout --quiet "v${SORTMERNA_VERSION}" && \
      echo "Download complete" && \
      ( echo "Extracting taxonomy database" && \
        cd "data/rRNA_databases/" && \
        tar -xf "silva_ids_acc_tax.tar.gz" \
      ) && \
      ( echo "moving rRNA fasta files to ${RRNA_REF_DIR}/" && \
        rsync --quiet --remove-source-files --archive "data/rRNA_databases/" "${RRNA_REF_DIR}/" \
      ) \
    ) && \
    ( echo "Cleaning up" && \
      rm -rf sortmerna/ \
    )

ENTRYPOINT ["sortmerna"]
CMD ["--help"]