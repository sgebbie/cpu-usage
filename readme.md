# CPU Usage

This is a simple utility to read /proc/stat and maintain a text based CPU usage
graph. This is intended to be used in conjunction with status bars like
i3-status.

An example graph might look like:

```
___▃▃▃▂▂▂█▂▁______▁_
```

The graph is output to:

- `${HOME}/.cpu-usage

Additionally, the last CPU usage calcation is record as a decimal percentage in:

- `${HOME}/.cpu-usage.spot

## Usage

```
cpu-usage [sampling-period-in-milliseconds] [graph-length] [foreground-toggle]
```

- sampling period: number of milliseconds in between readings and calculations.
- graph length: the number of bar graph characters to retain in the output.
- foreground toggle: `f` to run `cpu-usage` in the foreground.

Examples:

- `cpu-usage` - as-is, run in the background and sample every 1050ms, while
  outputting a 20 character graph.
- `cpu-usage 400` - sample every 0.4 seconds and record a 20 character graph
  to `${HOME}/.cpu-usage` while running in the background.
- `cpu-usage 2000 50 f` - sample once every 2 seconds and output a 50 character
  graph to `stdout` while running from the foreground.

## Integration

One potential usage is to configure i3-status to incorporate the graph in the
the bar's display.

This is can done by configuring `bar { status_command
/path/to/my/i3-status-mixin }` in  `~/config/i3/config`. And then creating a
mix-in that reads the graph.

```bash
#!/bin/bash
# shell script to prepend i3status with more stuff
# see: ~/.config/i3/config
# see: ~/.config/i3status/config

i3status | while :
do
	read line

	# skip the inital version lines in the i3 protocol
	if [[ $line =~ ^\{.version.*\}$ ]]; then echo $line || exit 1; continue; fi
	if [[ $line =~ ^\[$ ]]; then echo $line || exit 1; continue; fi
	
	[[ $line =~ ^(,)?(\[.*\])$ ]]
	I3COMMA=${BASH_REMATCH[1]}
	I3STATUS=${BASH_REMATCH[2]}

	# cpu graph
	CPU_USAGE=$(cat $HOME/.cpu-usage)
	CPU_SPOT=$(cat $HOME/.cpu-usage.spot)
	CPU_LOW="#4b8e0f"
	CPU_MEDIUM="#968f01"
	CPU_HIGH="#965a01"
	CPU_MAX="#931007"
	CPU_COLOUR=${CPU_LOW}
	if [ ${CPU_SPOT} -gt 15 ]; then CPU_COLOUR=${CPU_MEDIUM}; fi
	if [ ${CPU_SPOT} -gt 75 ]; then CPU_COLOUR=${CPU_HIGH}; fi
	if [ ${CPU_SPOT} -gt 99 ]; then CPU_COLOUR=${CPU_MAX}; fi

	echo -n ${I3COMMA}

	CPU_STATUS="{ \"name\":\"cpu-graph\", \"color\":\"${CPU_COLOUR}\", \"markup\":\"none\", \"instance\":0, \"full_text\":\"${CPU_USAGE}\" }"
	echo ${I3STATUS} | jq -c ". |= [ ${CPU_STATUS} ] + ."
done
