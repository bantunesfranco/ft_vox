/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: null <null@student.codam.nl>                 +#+                     */
/*                                                   +#+                      */
/*   Created: 2025/12/21 19:57:58 by null          #+#    #+#                 */
/*   Updated: 2025/12/21 19:57:58 by null          ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   main.cpp                                           :+:    :+:            */
/*                                                     +:+                    */
/*   By: bfranco <bfranco@student.codam.nl>           +#+                     */
/*                                                   +#+                      */
/*   Created: 2024/04/21 14:36:49 by bfranco       #+#    #+#                 */
/*   Updated: 2024/09/08 23:12:11 by bfranco       ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

#include <cstdlib>
#include <memory>

#include "App.hpp"


int main()
{
	std::map<settings_t, bool> settings = {{VOX_FULLSCREEN, false}, {VOX_RESIZE, true}, {VOX_DECORATED, true}, {VOX_MAXIMIZED, false}};

	const auto app = std::make_unique<App>(1440, 720, "PotatoCraft", settings);
	if (!app.get())
		return (EXIT_FAILURE);
	
	app->run();

	return (EXIT_SUCCESS);
}