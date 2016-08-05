

for SOURCE in *.[ch] ; do

	echo "Processing: ${SOURCE}"

	if [ -e src/${SOURCE} ]; then
		echo "   src/${SOURCE} exists, remove ./${SOURCE}"
		echo "rm ${SOURCE}"
		rm ${SOURCE}
	elif [ -e misc/${SOURCE} ]; then
		echo "   misc/${SOURCE} exists, remove ./${SOURCE}"
		echo "rm ${SOURCE}"
		rm ${SOURCE}
	else
		echo "   [src|misc]/${SOURCE} does not exist, do nothing !!!!!!!!!!!!!!"
	fi

done