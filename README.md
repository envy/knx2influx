# knx2influx

This tool acts as a simple data gateway from your KNX/IP bus to InfluxDB.

It listens on the KNX/IP multicast address for telegrams and writes the data to InfluxDB.

## Prerequisites

You need a KNX router that sends out KNX/IP multicast packets. Furthermore, you need to configure the filter table of the router to pass the telegrams you want monitored to the IP line.

## Dependencies

This program depends on `libcurl`.

## How to build

`make knx2influx`

## Example config

Minimal config is:

```json
{
  "host": "http://influxhost:8086",
  "database": "dbname",
  "gas": []
}
```

`gas` is a list of objects and describes, which group addresses should be monitored and what type the data has. The syntax is:

```json
{
  "ga": "1/2/3",
  "series": "my_measurement",
  "dpt": 9,
  "subdpt": 1,
  "log_only": false,
  "convert_to_int": false,
}
```

`ga` is the required group address, is this case 1/2/3.
`series` is the required name of the measurement in InfluxDB that it should write the data to.
`log_only` is optional (default `false`) and will prevent sending the data to InfluxDB. Instead the line protocol string will only be logged to standard out.
`dpt` is the required main Datapoint Type (DPT) of the data. The following DPTs are supported:
* `1`: Mapped to boolean.
* `2`: Mapped to two booleans, one for the data, one for the control.
* `5`: 1 Byte unsigned integer.
* `6`: 1 Byte signed integer.
* `7`: 2 Byte unsigned integer.
* `8`: 2 Byte signed integer.
* `9`: 2 Byte float.
* `12`: 4 Byte unsigned integer.
* `13`: 4 Byte signed integer.
* `14`: 4 Byte float.
* `232`: 3 Byte color. Will produce three fields `red`, `green` and `blue`.
* `60000`: 1 Byte HVAC Status (non-standard).

`convert_to_int`: Convert boolean values to integers when saving in InfluxDB. Currently only valid for DPT 1, 2, 5 and 22.

`subdpt` is the optional (default `1`) sub Datapoint Type of the data. This is currently only used for DPT 5 to implement scaling:
* `1`: (used with DPT 5 as 5.001): Scale the 1 Byte to 0..100, i.e. do `value/255 * 100`. This will produce a float instead of an integer if `convert_to_int` is not used.
* `3`: (used with DPT 5 as 5.003): Scale the 1 Byte to 0..360, i.e. do `value/255 * 360`. This will produce a float instead of an integer if `convert_to_int` is not used.
* `4`: (used with DPT 5 as 5.004): Do not scale at all. This will produce an integer.

Optionally, an additional list of tags can be given:

```json
{
  "ga": "1/2/3",
  "series": "my_measurement",
  "dpt": 9,
  "tags": [ "tag1=mytag", "another=tag" ]
}
```

Optionally, a blacklist of senders can be implemented. Telegrams from these senders (e.g., 1.2.3 and 3.4.10) are ignored:

```json
{
  "ga": "1/2/3",
  "series": "my_measurement",
  "dpt": 9,
  "ignored_senders": [ "1.0.1", "3.4.10" ]
}
```

The same group address can be specified multiple times, e.g., to write to two series or to write data from one set of senders to one series and from another set of senders to a different series.

### Sender tags

It is possible to add additional tags based on the sender like so:

```json
{
  // ... rest of the config
  "sender_tags": {
    "1.2.3": ["foo=bar"]
  }
}
```

This will add the tag `foo=bar` every time `1.2.3` is the sender in addition to the other tags.

### GA tags

Like sender tags, additional tags can be added based on received GA:

```json
{
  // ... rest of the config
  "ga_tags": {
    "1/2/3": [ "foo=bar" ]
  }
}
```

This will add the tag `foo=bar` every time GA `1/2/3`is the destination in addition to the other tags.

### Read on start up

knx2influx can issue a read on start up like so:

```json
{
  "read_on_startup": [ "1/2/3" ],
}
```

This will cause a read request to be issued to `1/2/3`on start up.

### Periodic read

knx2influx can also periodically issue reads like so:

```json
{
  "periodic_read": {
    "60": [ "1/2/3" ]
  },
}
```

This will issue a periodic read every 60 seconds to `1/2/3`.

### PA and GA wildcards

Every time the configuration expects a physical address or a group address as a key, wildcards can be used:

```json
{
  "gas": [
    {
      "ga": "1/2/3",
      // will only apply to GA 1/2/3
    },
    {
      "ga": "1/2/[10-25]",
      // will apply for GA 1/2/10, 1/2/11, ... and 1/2/25
    },
    {
      "ga": "1/*/3",
      // will apply for any GA with main group 1 and address 3, no matter which middle group
    }
  ]
}
```

Wildcards can be mixed: `*/*/*` will match any GA, `[1-4]/*/[200-230]` will match all GAs from main group 1, 2, 3 and 4 and address in range 200 to 230, no matter which middle group.
