mkdir -p gen

rm gen/*

SPEC_TEXT_DOCS=spec/document/core/text/*\.rst

awk '/production\{/,/^[ \t\r\n]*$/' ${SPEC_TEXT_DOCS} |
  sed s/\\\\phantom\{//g > gen/productions.txt

pushd gen

csplit -z productions.txt /\\production\{/ {*}

for FILE in `ls xx*`
do
  sed -E 's/[ \t\r\n]+/ /g' ${FILE} |
    sed -z 's/\n//g' |
    sed -E 's/\.\..*$//g' |
    sed 's/^ //g' |
    sed 's/ $//g' |
    sed 's/\\end{array}$//g' |
    sed 's/ $//g' |
    sed 's/\\end{array}$//g' |
    sed 's/\\\\//g' |
    sed -E 's/\\text\{([^\}]*)\}/"\1"/g' |
    sed 's/&&|&/\n|/g' > tmp
  mv tmp ${FILE}
done

popd


#  sed -E 's/\\phantom\{([^\}]*)\}/\\1/g' > gen/productions_orig.txt

#awk '/production\{/,/^[ \t\r\n]*$/' ${SPEC_TEXT_DOCS} |
#  grep -v "^   pair" |
#  grep -v "^\.\." |
#  sed s/\\\\production/\\n\\\\production/g |
#  sed s/\\\\production\{[^\}]*\}//g |
#  sed s/\\\\end\{array\}//g |
#  sed s/\\\^\\\\ast/*/g |
#  sed s/\\\&::=\\\&/:/g |
#  sed s/^[\\\ ]*\\\&//g |
#  sed s/\\\\T//g |
#  sed -E 's/\\text\{([^\}]*)\}/"\1"/g' |
#  sed s/\\\\\\\\//g |
#  sed 's/&&|&/|/g' > gen/productions.txt
