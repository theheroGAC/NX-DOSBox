#!/bin/sh
set -e

PORT_DIR=$1
if [ -z "$PORT_DIR" ]; then
	echo "usage: $0 <port-dir>" >&2
	exit 1
fi
PORT_DIR=$(cd "$PORT_DIR" && pwd)

eval "$(make -s -C "$PORT_DIR" ninja-vars)"

wpath() {
	if command -v cygpath >/dev/null 2>&1; then
		cygpath -m "$1"
	else
		echo "$1"
	fi
}
OUT_DIR=$(wpath "$OUT_DIR")
APP_ICON=$(wpath "$APP_ICON")

SCRATCH="$PORT_DIR/build/tmp"
mkdir -p "$SCRATCH"
TMP="$SCRATCH" TEMP="$SCRATCH" TMPDIR="$SCRATCH"
export TMP TEMP TMPDIR

NPROC=${NPROC:-4}
export CXX CXXFLAGS TOP
echo "$SOURCES" | tr ' ' '\n' | xargs -P "$NPROC" -I{} sh -c '
	src=$1
	obj="build/$(echo "$src" | sed -e "s#^$TOP/##" -e "s#\.cpp\$#.o#")"
	if [ -f "$obj.d" ] && [ "$obj.d" -nt "$src" ]; then exit 0; fi
	mkdir -p "$(dirname "$obj")"
	$CXX $CXXFLAGS -MMD -MF "$obj.d" "$src" 2>/dev/null || :
' _ {}

objects=""
edges=""
for src in $SOURCES; do
	obj="build/$(echo "$src" | sed -e "s#^$TOP/##" -e 's#\.cpp$#.o#')"
	headers=""
	if [ -f "$obj.d" ]; then
		headers=$(awk '{ gsub(/\\/, " "); seen=0; for (i=1;i<=NF;i++) { if ($i==":") {seen=1; continue} if (seen) print $i } }' "$obj.d" \
			| sort -u | tr '\n' ' ')
	fi
	edges="$edges
build $obj: cc $src${headers:+ |}$headers"
	objects="$objects $obj"
done

cat > "$PORT_DIR/build.ninja" <<EOF

ninja_required_version = 1.8

rule cc
  command = "$CXX" $CXXFLAGS -c \$in -o \$out
  description = cc \$out

rule link
  command = "$CXX" $LDFLAGS \$in $LIBPATHS $LIBS -o \$out
  description = link \$out

rule nacp
  command = "$NACPTOOL" --create "$APP_TITLE" "$APP_AUTHOR" "$APP_VERSION" \$out
  description = nacp \$out

rule nro
  command = "$ELF2NRO" \$in \$out --nacp=$OUT_REL/$TARGET.nacp --icon="$APP_ICON" $NROFLAGS
  description = nro \$out
$edges

build $OUT_REL/$TARGET.nacp: nacp Makefile
build $OUT_REL/$TARGET.elf: link $objects
build $OUT_REL/$TARGET.nro: nro $OUT_REL/$TARGET.elf | $OUT_REL/$TARGET.nacp

default $OUT_REL/$TARGET.nro
EOF

echo "wrote $PORT_DIR/build.ninja"
