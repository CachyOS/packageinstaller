if [ -e "$1" ]; then
	echo ""
	echo "Preparing stuff.."
	echo ""
	sudo bash - <$1
fi

echo ""
echo "Installing packages.."
echo ""
sudo pacman -Sy
installable_packages=$(comm -12 <(pacman -Slq | sort) <(sed s/\\s/\\n/g - <$2 | sort))
sudo pacman -Su --needed $installable_packages

if [ -e "$3" ]; then
	echo ""
	echo "Enabling services.."
	echo ""
	sudo bash - <$3
fi

echo ""
read -p "Press enter to finish"
