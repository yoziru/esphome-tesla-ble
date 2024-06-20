FROM esphome/esphome

COPY . /app
# Compile
RUN esphome compile
